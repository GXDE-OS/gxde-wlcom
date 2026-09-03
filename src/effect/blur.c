// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include <wlr/types/wlr_matrix.h>
#include <wlr/util/region.h>

#include <kywc/boxes.h>
#include <kywc/log.h>

#include "blur_down_frag.h"
#include "blur_first_down_frag.h"
#include "blur_first_down_vert.h"
#include "blur_tex_rgba_frag.h"
#include "blur_tex_vert.h"
#include "blur_up_frag.h"
#include "blur_vert.h"

#include "effect/blur.h"
#include "effect/effect.h"
#include "effect_p.h"
#include "render/opengl.h"
#include "render/profile.h"
#include "scene/scene.h"
#include "util/time.h"

#define BLUR_STRENGTH_KEY "blur_strength"
#define NOISE_STRENGTH_KEY "noise_strength"
#define DEFAULT_BLUR_STRENGTH 4
#define MIN_BLUR_STRENGTH 1
#define MAX_BLUR_STRENGTH 15
#define DEFAULT_NOISE_STRENGTH 0
#define MIN_NOISE_STRENGTH 0
#define MAX_NOISE_STRENGTH 14

// rtt = render to texture = fbo + texture
struct glrtt_pool_texture {
    struct wl_list link;
    GLuint fbo;
    GLuint texture;
    GLsizei width;
    GLsizei height;
    bool used;
    uint32_t time;
};

struct glrtt_pool {
    GLenum format;
    GLint filter;
    uint32_t timeout;
    struct wl_list textures;
};

static struct glrtt_pool *glrtt_pool_create(GLenum format, GLint filter, uint32_t timeout)
{
    struct glrtt_pool *pool = calloc(1, sizeof(*pool));
    if (!pool) {
        return NULL;
    }
    pool->format = format;
    pool->filter = filter;
    pool->timeout = timeout;
    wl_list_init(&pool->textures);
    return pool;
}

static void glrtt_pool_destroy(struct glrtt_pool *pool)
{
    struct glrtt_pool_texture *texture, *tmp;
    wl_list_for_each_safe(texture, tmp, &pool->textures, link) {
        glDeleteFramebuffers(1, &texture->fbo);
        glDeleteTextures(1, &texture->texture);
        wl_list_remove(&texture->link);
        free(texture);
    }
    free(pool);
}

static struct glrtt_pool_texture *glrtt_pool_get_texture(struct glrtt_pool *pool, GLsizei width,
                                                         GLsizei height)
{
    struct glrtt_pool_texture *texture = NULL;

    // find cached texture
    bool cached = false;
    struct glrtt_pool_texture *texture_tmp;
    wl_list_for_each(texture_tmp, &pool->textures, link) {
        if (!texture_tmp->used && texture_tmp->width == width && texture_tmp->height == height) {
            texture = texture_tmp;
            cached = true;
            break;
        }
    }
    // new texture
    if (!cached) {
        texture = calloc(1, sizeof(*texture));
        if (!texture) {
            return NULL;
        }

        glGenTextures(1, &texture->texture);
        glBindTexture(GL_TEXTURE_2D, texture->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, pool->filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, pool->filter);
        glTexImage2D(GL_TEXTURE_2D, 0, pool->format, width, height, 0, pool->format,
                     GL_UNSIGNED_BYTE, 0);

        GLint old_fbo;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
        glGenFramebuffers(1, &texture->fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, texture->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               texture->texture, 0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            kywc_log(KYWC_ERROR, "glrtt pool failed to fbo(%d) attach texture(%d). status=%d",
                     texture->fbo, texture->texture, status);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);

        texture->width = width;
        texture->height = height;
        wl_list_insert(&pool->textures, &texture->link);
    }

    texture->used = true;
    return texture;
}

static void glrtt_pool_release_texture(struct glrtt_pool *pool, struct glrtt_pool_texture *texture)
{
    if (!texture) {
        return;
    }
    texture->used = false;
    texture->time = current_time_msec();
}

static void glrtt_pool_release_timeout_texture(struct glrtt_pool *pool)
{
    struct glrtt_pool_texture *texture, *tmp;
    wl_list_for_each_safe(texture, tmp, &pool->textures, link) {
        if (!texture->used && (current_time_msec() - texture->time) > pool->timeout) {
            glDeleteFramebuffers(1, &texture->fbo);
            glDeleteTextures(1, &texture->texture);
            wl_list_remove(&texture->link);
            free(texture);
        }
    }
}

struct blur_tex_program {
    GLuint id;

    struct {
        GLint proj;
        GLint tex_proj;
        GLint shape_proj;
        GLint tex;
        GLint alpha;
        GLint noise_strength;
        GLint anti_aliasing;
        GLint aspect;
        GLint rounded_corner_radius;
        GLint pos_attrib;
        GLint sdfpos_attrib;

        GLint offset;
        GLint halfpixel;
    } shaders;
};

struct first_blur_shaders {
    GLint uv2tex;
    GLint min_uv;
    GLint max_uv;
};

struct blur_program {
    GLuint id;

    struct {
        GLint texture;
        GLint offset;
        GLint position;
        GLint halfpixel;
    } shaders;

    void *extension_data;
};

struct blur_output_data {
    struct wl_list link;
    pixman_region32_t unaffected_region;
    pixman_region32_t damage_blur_region;

    struct ky_scene_output *output;
    struct wl_listener output_destroy;

    struct wlr_buffer *output_buffer;
};

enum blur_program_type {
    BLUR_PROGRAM_FIRST_DOWN,
    BLUR_PROGRAM_DOWN,
    BLUR_PROGRAM_UP,
    BLUR_PROGRAM_TYPE_COUNT
};

static struct blur_data {
    struct blur_effect *effect;
    struct wl_list output_data;
    struct blur_output_data *current_output_data;
    struct ky_opengl_texture *current_output_texture;

    float offset;
    uint32_t iterations;
    struct blur_program blur_prog[BLUR_PROGRAM_TYPE_COUNT];
    struct blur_tex_program blur_tex_prog;
} effect_blur_data = { 0 };

struct blur_render_config {
    struct ky_opengl_renderer *renderer;
};

static struct blur_render_config blur_config = {
    .renderer = NULL,
};

struct blur_effect {
    struct effect *effect;
    struct ky_scene *scene;
    struct glrtt_pool *glrtt_pool;

    int blur_strength;
    int noise_strength;

    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;
};

struct blur_level {
    uint32_t iterations;
    float offset;
};

static const struct blur_level blur_levels[] = {
    { 1, 1.5f },     { 1, 2.0f }, { 2, 2.5f },     { 2, 3.0f },     { 3, 2.6f },
    { 3, 3.2f },     { 3, 3.8f }, { 3, 4.4f },     { 3, 5.0f },     { 4, 3.83333f },
    { 4, 4.66667f }, { 4, 5.5f }, { 4, 6.33333f }, { 4, 7.16667f }, { 4, 8.0f },
};

#define MAX_QUADS 86 // 4kb

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

static void blur_output_data_destroy(struct blur_output_data *data);
static void handle_output_destroy(struct wl_listener *listener, void *data);

static void get_blur_level(const struct blur_info *info, uint32_t *iterations, float *offset)
{
    if (info->custom_level || !effect_blur_data.effect) {
        *iterations = info->iterations;
        *offset = info->offset;
        return;
    }

    const struct blur_level *level =
        &blur_levels[effect_blur_data.effect->blur_strength - MIN_BLUR_STRENGTH];
    *iterations = level->iterations;
    *offset = level->offset;
}

static int calculate_blur_radius(int iterations, float offset)
{
    if (offset <= 0.0f) {
        return 0;
    }

    return ceilf(ldexpf(offset, iterations + 1));
}

#if 0
static void save_texture_to_rgba(int frame_count, GLuint texture_id, GLuint fb, int width,
                                 int height, const char *prefix)
{
    char path[255] = { 0 };
    snprintf(path, 50, "/tmp/ky_%dx%d_rgba_%s_%d_%d%s", width, height, prefix, fb, frame_count,
             ".rgb");
    mkstemps(path, 4);

    int fileszie = width * height * 4;
    void *pixles = calloc(fileszie, sizeof(char));

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    GLuint pbo;
    glGenBuffers(1, &pbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, fileszie, NULL, GL_STREAM_READ);

    if (fb > 0 && texture_id <= 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        /* FBO attach texture */
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_id,
                               0);
    }
    /**
     * read texture data to PBO.
     * nullptr indicates reading data to PBO, not to cpu memory.
     */
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    /* Map PBO to CPU address space */
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
    void *pixels = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, fileszie, GL_MAP_READ_BIT);
    if (pixels) {
        FILE *f = fopen(path, "wb+");
        if (!f) {
            goto failed;
        }
        fwrite(pixels, fileszie, 1, f);
        fclose(f);
    }
failed:
    /* unmap PBO */
    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    glDeleteBuffers(1, &pbo);
    glDeleteFramebuffers(1, &fbo);

    free(pixles);
}
#endif

static void blur_program_generate(struct blur_program *prog, const char *vertex_source,
                                  const char *frag_source)
{
    GLuint prog_id = ky_opengl_create_program(blur_config.renderer, vertex_source, frag_source);
    if (prog_id > 0) {
        prog->id = prog_id;
        prog->shaders.position = glGetAttribLocation(prog_id, "position");
        prog->shaders.texture = glGetUniformLocation(prog_id, "texture");
        prog->shaders.offset = glGetUniformLocation(prog_id, "offset");
        prog->shaders.halfpixel = glGetUniformLocation(prog_id, "halfpixel");
    }
}

static void first_blur_program_generate(struct blur_program *prog, const char *vertex_source,
                                        const char *frag_source)
{
    blur_program_generate(prog, vertex_source, frag_source);
    if (prog->id <= 0) {
        return;
    }

    struct first_blur_shaders *shaders = calloc(1, sizeof(*shaders));
    if (shaders) {
        shaders->uv2tex = glGetUniformLocation(prog->id, "uv2tex");
        shaders->max_uv = glGetUniformLocation(prog->id, "max_uv");
        shaders->min_uv = glGetUniformLocation(prog->id, "min_uv");
        prog->extension_data = shaders;
    }
}

static struct blur_program *get_or_generate_blur_program(void)
{
    struct blur_program *blur_prog = effect_blur_data.blur_prog;
    if (blur_prog[BLUR_PROGRAM_FIRST_DOWN].id <= 0) {
        first_blur_program_generate(&blur_prog[BLUR_PROGRAM_FIRST_DOWN], blur_first_down_vert,
                                    blur_first_down_frag);
    }
    if (blur_prog[BLUR_PROGRAM_DOWN].id <= 0) {
        blur_program_generate(&blur_prog[BLUR_PROGRAM_DOWN], blur_vert, blur_down_frag);
    }

    if (blur_prog[BLUR_PROGRAM_UP].id <= 0) {
        blur_program_generate(&blur_prog[BLUR_PROGRAM_UP], blur_vert, blur_up_frag);
    }

    for (int i = 0; i < BLUR_PROGRAM_TYPE_COUNT; ++i) {
        if (blur_prog[i].id <= 0) {
            kywc_log(KYWC_ERROR, "Blur shader compile error!");
            return NULL;
        }
    }

    return blur_prog;
}

static struct blur_tex_program *get_or_generate_blur_text_program(void)
{
    struct blur_tex_program *blur_tex_prog = &effect_blur_data.blur_tex_prog;
    if (blur_tex_prog->id > 0) {
        return blur_tex_prog;
    }
    GLuint prog = ky_opengl_create_program(blur_config.renderer, blur_tex_vert, blur_tex_rgba_frag);
    if (prog > 0) {
        blur_tex_prog->id = prog;
        blur_tex_prog->shaders.proj = glGetUniformLocation(prog, "proj");
        blur_tex_prog->shaders.tex_proj = glGetUniformLocation(prog, "tex_proj");
        blur_tex_prog->shaders.shape_proj = glGetUniformLocation(prog, "shape_proj");
        blur_tex_prog->shaders.tex = glGetUniformLocation(prog, "tex");
        blur_tex_prog->shaders.alpha = glGetUniformLocation(prog, "alpha");
        blur_tex_prog->shaders.noise_strength = glGetUniformLocation(prog, "noiseStrength");
        blur_tex_prog->shaders.anti_aliasing = glGetUniformLocation(prog, "antiAliasing");
        blur_tex_prog->shaders.aspect = glGetUniformLocation(prog, "aspect");
        blur_tex_prog->shaders.rounded_corner_radius =
            glGetUniformLocation(prog, "roundedCornerRadius");
        blur_tex_prog->shaders.pos_attrib = glGetAttribLocation(prog, "pos");
        blur_tex_prog->shaders.sdfpos_attrib = glGetAttribLocation(prog, "sdfpos");
        blur_tex_prog->shaders.offset = glGetUniformLocation(prog, "offset");
        blur_tex_prog->shaders.halfpixel = glGetUniformLocation(prog, "halfpixel");
        return blur_tex_prog;
    }
    kywc_log(KYWC_ERROR, "Blur shader compile error!");
    return NULL;
}

static void blur_data_destroy(struct blur_data *data)
{
    ky_egl_make_current(blur_config.renderer->egl, NULL);

    for (int i = 0; i < BLUR_PROGRAM_TYPE_COUNT; ++i) {
        if (data->blur_prog[i].id <= 0) {
            continue;
        }

        if (data->blur_prog[i].extension_data) {
            free(data->blur_prog[i].extension_data);
        }

        glDeleteProgram(data->blur_prog[i].id);
    }

    if (data->blur_tex_prog.id > 0) {
        glDeleteProgram(data->blur_tex_prog.id);
    }

    struct blur_output_data *output_data, *tmp;
    wl_list_for_each_safe(output_data, tmp, &data->output_data, link) {
        blur_output_data_destroy(output_data);
    }
}

static void set_proj_matrix(GLint loc, float proj[9], const struct kywc_box *box)
{
    float gl_matrix[9];
    wlr_matrix_identity(gl_matrix);
    wlr_matrix_translate(gl_matrix, box->x, box->y);
    wlr_matrix_scale(gl_matrix, box->width, box->height);
    wlr_matrix_multiply(gl_matrix, proj, gl_matrix);
    glUniformMatrix3fv(loc, 1, GL_FALSE, gl_matrix);
}

static void set_tex_matrix(GLint loc, enum wl_output_transform trans, const struct kywc_fbox *box)
{
    float tex_matrix[9];
    wlr_matrix_identity(tex_matrix);
    wlr_matrix_translate(tex_matrix, box->x, box->y);
    wlr_matrix_scale(tex_matrix, box->width, box->height);
    wlr_matrix_translate(tex_matrix, .5, .5);

    /**
     * since textures have a different origin point we have to transform
     * differently if we are rotating
     */
    if (trans & WL_OUTPUT_TRANSFORM_90) {
        wlr_matrix_transform(tex_matrix, wlr_output_transform_invert(trans));
    } else {
        wlr_matrix_transform(tex_matrix, trans);
    }
    wlr_matrix_translate(tex_matrix, -.5, -.5);

    glUniformMatrix3fv(loc, 1, GL_FALSE, tex_matrix);
}

static void render_iteration(struct glrtt_pool_texture *in, struct glrtt_pool_texture *out,
                             int width, int height)
{
    KY_PROFILE_RENDER_ZONE(&blur_config.renderer->wlr_renderer, gzone, __func__);
    glBindFramebuffer(GL_FRAMEBUFFER, out->fbo);
    glViewport(0, 0, out->width, out->height);

    glBindTexture(GL_TEXTURE_2D, in->texture);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    KY_PROFILE_RENDER_ZONE_END(&blur_config.renderer->wlr_renderer);
}

static struct glrtt_pool_texture *blur_first_down(struct blur_data *data,
                                                  struct ky_opengl_buffer *gl_buffer,
                                                  struct kywc_box *blur_src_box)
{
    struct blur_program *first_down_prog = &data->blur_prog[BLUR_PROGRAM_FIRST_DOWN];
    struct first_blur_shaders *extension_shaders = first_down_prog->extension_data;
    if (!extension_shaders) {
        return NULL;
    }

    KY_PROFILE_RENDER_ZONE(&blur_config.renderer->wlr_renderer, gzone, __func__);

    int sample_width = blur_src_box->width / 2;
    int sample_height = blur_src_box->height / 2;
    if (sample_width <= 0 || sample_height <= 0) {
        return NULL;
    }

    struct glrtt_pool *glrtt_pool = data->effect->glrtt_pool;
    struct glrtt_pool_texture *out_tex =
        glrtt_pool_get_texture(glrtt_pool, sample_width, sample_height);
    if (!out_tex) {
        return NULL;
    }

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    glDisable(GL_BLEND);
    // down scale
    glUseProgram(first_down_prog->id);
    glEnableVertexAttribArray(first_down_prog->shaders.position);
    GLfloat pos_vertex[8] = { -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f };
    glVertexAttribPointer(first_down_prog->shaders.position, 2, GL_FLOAT, GL_FALSE, 0, pos_vertex);

    struct ky_opengl_texture *gl_texture = data->current_output_texture;
    if (!gl_texture) {
        struct wlr_texture *src_tex =
            ky_opengl_texture_from_buffer(&blur_config.renderer->wlr_renderer, gl_buffer->buffer);
        gl_texture = ky_opengl_texture_from_wlr_texture(src_tex);
    }
    if (!gl_texture) {
        return NULL;
    }

    int width = gl_texture->wlr_texture.width;
    int height = gl_texture->wlr_texture.height;
    struct kywc_fbox src_fbox = {
        .x = blur_src_box->x * 1.0f / width,
        .y = blur_src_box->y * 1.0f / height,
        .width = blur_src_box->width * 1.0f / width,
        .height = blur_src_box->height * 1.0f / height,
    };
    set_tex_matrix(extension_shaders->uv2tex, WL_OUTPUT_TRANSFORM_NORMAL, &src_fbox);
    glUniform2f(extension_shaders->min_uv, src_fbox.x, src_fbox.y);
    /**
     * the srcbox may be greater than 1, and texture sampling may exceed the texture boundary.
     * using GL_CAMP_TO_EDGE can ensure correct blur results.
     */
    glUniform2f(extension_shaders->max_uv, src_fbox.x + src_fbox.width,
                src_fbox.y + src_fbox.height);

    glUniform1i(first_down_prog->shaders.texture, 0);
    glUniform1f(first_down_prog->shaders.offset, data->offset);
    glUniform2f(first_down_prog->shaders.halfpixel, 0.5f / width, 0.5f / height);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(gl_texture->target, gl_texture->tex);
    glTexParameteri(gl_texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(gl_texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(gl_texture->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(gl_texture->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, out_tex->fbo);
    glViewport(0, 0, out_tex->width, out_tex->height);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(gl_texture->target, 0);

    glDisableVertexAttribArray(first_down_prog->shaders.position);
    glUseProgram(0);
    glEnable(GL_BLEND);

    if (!data->current_output_texture) {
        wlr_texture_destroy(&gl_texture->wlr_texture);
    }
    KY_PROFILE_RENDER_ZONE_END(&blur_config.renderer->wlr_renderer);
    return out_tex;
}

static void blur_fb0(struct blur_data *data, struct glrtt_pool_texture *src_tex)
{
    struct glrtt_pool *glrtt_pool = data->effect->glrtt_pool;
    // iterations must > 0
    int iterations = data->iterations - 1;
    float offset = data->offset;
    int width = src_tex->width;
    int height = src_tex->height;

    KY_PROFILE_RENDER_ZONE(&blur_config.renderer->wlr_renderer, gzone, __func__);
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    struct blur_program *prog = data->blur_prog;
    struct blur_program *down_prog = &prog[BLUR_PROGRAM_DOWN];
    struct blur_program *up_prog = &prog[BLUR_PROGRAM_UP];

    glDisable(GL_BLEND);
    // down scale
    glUseProgram(down_prog->id);
    glEnableVertexAttribArray(down_prog->shaders.position);
    GLfloat pos_vertex[8] = { -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f };
    glVertexAttribPointer(down_prog->shaders.position, 2, GL_FLOAT, GL_FALSE, 0, pos_vertex);
    glUniform1f(down_prog->shaders.offset, offset);
    glUniform1i(down_prog->shaders.texture, 0);

    struct glrtt_pool_texture *last_tex = src_tex;
    for (int i = 0; i < iterations; i++) {
        int sample_width = MAX(width / (1 << (i + 1)), 1);
        int sample_height = MAX(height / (1 << (i + 1)), 1);

        // first iter input use src_tex
        struct glrtt_pool_texture *in_tex = last_tex;
        struct glrtt_pool_texture *out_tex =
            glrtt_pool_get_texture(glrtt_pool, sample_width, sample_height);

        glUniform2f(down_prog->shaders.halfpixel, 0.5f / sample_width, 0.5f / sample_height);
        render_iteration(in_tex, out_tex, sample_width, sample_height);

        if (i != 0) {
            glrtt_pool_release_texture(glrtt_pool, in_tex);
        }
        last_tex = out_tex;
    }
    glDisableVertexAttribArray(down_prog->shaders.position);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);

    // up scale
    glUseProgram(up_prog->id);
    glEnableVertexAttribArray(up_prog->shaders.position);
    glVertexAttribPointer(up_prog->shaders.position, 2, GL_FLOAT, GL_FALSE, 0, pos_vertex);
    glUniform1f(up_prog->shaders.offset, offset);
    glUniform1i(up_prog->shaders.texture, 0);
    for (int i = iterations - 1; i >= 0; i--) {
        int sample_width = MAX(width / (1 << i), 1);
        int sample_height = MAX(height / (1 << i), 1);

        // last iter output use src_tex
        struct glrtt_pool_texture *in_tex = last_tex;
        struct glrtt_pool_texture *out_tex =
            i == 0 ? src_tex : glrtt_pool_get_texture(glrtt_pool, sample_width, sample_height);

        glUniform2f(up_prog->shaders.halfpixel, 0.5f / sample_width, 0.5f / sample_height);
        render_iteration(in_tex, out_tex, sample_width, sample_height);

        glrtt_pool_release_texture(glrtt_pool, in_tex);
        last_tex = out_tex;
    }
    glDisableVertexAttribArray(up_prog->shaders.position);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    KY_PROFILE_RENDER_ZONE_END(&blur_config.renderer->wlr_renderer);
}

static void render(const struct kywc_box *box, const pixman_region32_t *blur, GLint attrib,
                   const struct wlr_box *sdf_box, GLint sdfpos_attrib)
{
    int rects_len;
    const pixman_box32_t *rects = pixman_region32_rectangles(blur, &rects_len);

    KY_PROFILE_RENDER_ZONE(&blur_config.renderer->wlr_renderer, gzone, __func__);
    glEnableVertexAttribArray(sdfpos_attrib);
    glEnableVertexAttribArray(attrib);
    for (int i = 0; i < rects_len;) {
        int batch = rects_len - i < MAX_QUADS ? rects_len - i : MAX_QUADS;
        int batch_end = batch + i;

        size_t vert_index = 0;
        GLfloat verts[MAX_QUADS * 6 * 4];
        for (; i < batch_end; i++) {
            const pixman_box32_t *rect = &rects[i];

            verts[vert_index++] = (GLfloat)(rect->x1 - box->x) / box->width;
            verts[vert_index++] = (GLfloat)(rect->y1 - box->y) / box->height;
            verts[vert_index++] = (GLfloat)(rect->x1 - sdf_box->x) / sdf_box->width;
            verts[vert_index++] = (GLfloat)(rect->y1 - sdf_box->y) / sdf_box->height;

            verts[vert_index++] = (GLfloat)(rect->x2 - box->x) / box->width;
            verts[vert_index++] = (GLfloat)(rect->y1 - box->y) / box->height;
            verts[vert_index++] = (GLfloat)(rect->x2 - sdf_box->x) / sdf_box->width;
            verts[vert_index++] = (GLfloat)(rect->y1 - sdf_box->y) / sdf_box->height;

            verts[vert_index++] = (GLfloat)(rect->x1 - box->x) / box->width;
            verts[vert_index++] = (GLfloat)(rect->y2 - box->y) / box->height;
            verts[vert_index++] = (GLfloat)(rect->x1 - sdf_box->x) / sdf_box->width;
            verts[vert_index++] = (GLfloat)(rect->y2 - sdf_box->y) / sdf_box->height;

            verts[vert_index++] = (GLfloat)(rect->x2 - box->x) / box->width;
            verts[vert_index++] = (GLfloat)(rect->y1 - box->y) / box->height;
            verts[vert_index++] = (GLfloat)(rect->x2 - sdf_box->x) / sdf_box->width;
            verts[vert_index++] = (GLfloat)(rect->y1 - sdf_box->y) / sdf_box->height;

            verts[vert_index++] = (GLfloat)(rect->x2 - box->x) / box->width;
            verts[vert_index++] = (GLfloat)(rect->y2 - box->y) / box->height;
            verts[vert_index++] = (GLfloat)(rect->x2 - sdf_box->x) / sdf_box->width;
            verts[vert_index++] = (GLfloat)(rect->y2 - sdf_box->y) / sdf_box->height;

            verts[vert_index++] = (GLfloat)(rect->x1 - box->x) / box->width;
            verts[vert_index++] = (GLfloat)(rect->y2 - box->y) / box->height;
            verts[vert_index++] = (GLfloat)(rect->x1 - sdf_box->x) / sdf_box->width;
            verts[vert_index++] = (GLfloat)(rect->y2 - sdf_box->y) / sdf_box->height;
        }

        glVertexAttribPointer(attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), verts);
        glVertexAttribPointer(sdfpos_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), verts + 2);
        glDrawArrays(GL_TRIANGLES, 0, batch * 6);
    }

    glDisableVertexAttribArray(attrib);
    glDisableVertexAttribArray(sdfpos_attrib);
    KY_PROFILE_RENDER_ZONE_END(&blur_config.renderer->wlr_renderer);
}

static void blur_render(struct ky_scene_render_target *target,
                        const struct blur_render_options *options,
                        const pixman_region32_t *blur_region, struct kywc_box *buffer_cpy_box)
{
    KY_PROFILE_RENDER_ZONE(&blur_config.renderer->wlr_renderer, gzone, __func__);

    GLint old_fbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);

    struct ky_opengl_render_pass *gl_pass =
        ky_opengl_render_pass_from_wlr_render_pass(target->render_pass);

    struct blur_data *data = &effect_blur_data;
    struct glrtt_pool *glrtt_pool = data->effect->glrtt_pool;
    get_blur_level(options->blur, &data->iterations, &data->offset);

    struct glrtt_pool_texture *src_tex = blur_first_down(data, gl_pass->buffer, buffer_cpy_box);
    if (!src_tex) {
        goto final;
    }

    blur_fb0(data, src_tex);

    struct blur_tex_program *prog = &data->blur_tex_prog;
    if (!prog) {
        goto final;
    }
    glUseProgram(prog->id);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_tex->texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glUniform1i(prog->shaders.tex, 0);
    glUniform1f(prog->shaders.offset, data->offset);
    glUniform2f(prog->shaders.halfpixel, 0.5f / src_tex->width, 0.5f / src_tex->height);

    float alpha = options->alpha ? *options->alpha : 1.0f;
    glUniform1f(prog->shaders.alpha, alpha);
    glUniform1f(prog->shaders.noise_strength, data->effect->noise_strength / 255.0f);

    struct kywc_fbox tex_src_fbox = { .x = 0, .y = 0, .width = 1.0f, .height = 1.0f };
    set_proj_matrix(prog->shaders.proj, gl_pass->projection_matrix, buffer_cpy_box);
    set_tex_matrix(prog->shaders.tex_proj, WL_OUTPUT_TRANSFORM_NORMAL, &tex_src_fbox);
    struct kywc_fbox shap_fbox = { .x = 0, .y = 0, .width = 1.0f, .height = 1.0f };
    set_tex_matrix(prog->shaders.shape_proj, target->transform, &shap_fbox);

    int width = options->dst_box->width;
    int height = options->dst_box->height;
    if (target->transform & WL_OUTPUT_TRANSFORM_90) {
        width = options->dst_box->height;
        height = options->dst_box->width;
    }

    glUniform1f(prog->shaders.aspect, width / (float)height);
    float half_height = (float)height * 0.5f;
    float one_pixel_distance = 1.0f / half_height; // shader distance scale
    glUniform1f(prog->shaders.anti_aliasing, one_pixel_distance * 0.5f);

    if (options->radius) {
        glUniform4f(prog->shaders.rounded_corner_radius, options->radius->rb * one_pixel_distance,
                    options->radius->rt * one_pixel_distance,
                    options->radius->lb * one_pixel_distance,
                    options->radius->lt * one_pixel_distance);
    } else {
        glUniform4f(prog->shaders.rounded_corner_radius, 0, 0, 0, 0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);
    render(buffer_cpy_box, blur_region, prog->shaders.pos_attrib, options->dst_box,
           prog->shaders.sdfpos_attrib);

    glrtt_pool_release_texture(glrtt_pool, src_tex);
    // call release_timeout in blur, better than frame_pre()
    // in frame_pre will pre release blur need use texture
    glrtt_pool_release_timeout_texture(glrtt_pool);

    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

final:
    KY_PROFILE_RENDER_ZONE_END(&blur_config.renderer->wlr_renderer);
}

static void get_copy_box(const pixman_region32_t *blur_region, const struct blur_info *blur_info,
                         const pixman_region32_t *damaged_blur_region, struct kywc_box *copy_box)
{
    (void)blur_info;
    (void)damaged_blur_region;

    /* Keep the temporary texture origin and dimensions fixed for the whole
     * blur surface.  A damage-sized texture changes the downsample phase on
     * every dock animation frame and produces the moving vertical seams. */
    const pixman_box32_t *blur_box = pixman_region32_extents(blur_region);
    *copy_box = (struct kywc_box){
        .x = blur_box->x1,
        .y = blur_box->y1,
        .width = blur_box->x2 - blur_box->x1,
        .height = blur_box->y2 - blur_box->y1,
    };
}

void blur_render_with_target(struct ky_scene_render_target *target,
                             const struct blur_render_options *options)
{
    if (options->blur == NULL || !blur_config.renderer ||
        !effect_blur_data.effect->effect->enabled || !get_or_generate_blur_text_program() ||
        !get_or_generate_blur_program() || (options->alpha && *options->alpha <= 0.0f)) {
        return;
    }

    pixman_region32_t blur_region;
    if (pixman_region32_not_empty(&options->blur->region)) {
        pixman_region32_init(&blur_region);
        pixman_region32_copy(&blur_region, &options->blur->region);
        pixman_region32_translate(&blur_region, options->lx - target->logical.x,
                                  options->ly - target->logical.y);
        ky_scene_render_region(&blur_region, target);
    } else {
        pixman_region32_init_rect(&blur_region, options->dst_box->x, options->dst_box->y,
                                  options->dst_box->width, options->dst_box->height);
    }

    pixman_region32_t damaged_blur_region;
    pixman_region32_init(&damaged_blur_region);
    pixman_region32_intersect(&damaged_blur_region, &blur_region, options->clip);
    if (pixman_region32_not_empty(&damaged_blur_region)) {
        struct kywc_box buffer_cpy_box;
        get_copy_box(&blur_region, options->blur, &damaged_blur_region, &buffer_cpy_box);
        blur_render(target, options, &damaged_blur_region, &buffer_cpy_box);
    }
    pixman_region32_fini(&damaged_blur_region);
    pixman_region32_fini(&blur_region);
}

static void node_for_each_blur_region(struct ky_scene_node *node,
                                      struct ky_scene_render_target *target, int lx, int ly,
                                      struct blur_data *data, pixman_region32_t *half_expand_damage)
{
    if (!ky_scene_node_is_visible(node)) {
        return;
    }

    if (node->type == KY_SCENE_NODE_TREE) {
        struct ky_scene_tree *tree = ky_scene_tree_from_node(node);
        struct ky_scene_node *child;
        wl_list_for_each_reverse(child, &tree->children, link) {
            node_for_each_blur_region(child, target, lx + child->x, ly + child->y, data,
                                      half_expand_damage);
        }
        return;
    }

    pixman_region32_t *damage_blur = &data->current_output_data->damage_blur_region;
    /* the blur damage must be refresh */
    if (pixman_region32_not_empty(damage_blur)) {
        struct wlr_box box;
        node->impl.get_bounding_box(node, &box);

        pixman_region32_t blur_background;
        pixman_region32_init_rect(&blur_background, 0, 0, box.width, box.height);
        if (pixman_region32_not_empty(&node->clip_region)) {
            pixman_region32_intersect(&blur_background, &blur_background, &node->clip_region);
        }
        pixman_region32_translate(&blur_background, box.x + lx, box.y + ly);
        pixman_region32_intersect(&blur_background, &blur_background, damage_blur);
        pixman_region32_union(&node->visible_region, &node->visible_region, &blur_background);
        /* TODO: damage_blur sub the node opaque region */
        pixman_region32_fini(&blur_background);
    }

    if (!node->has_blur) {
        return;
    }

    pixman_region32_t blur_region;
    pixman_region32_init(&blur_region);
    if (pixman_region32_not_empty(&node->blur.region)) {
        pixman_region32_copy(&blur_region, &node->blur.region);
        pixman_region32_translate(&blur_region, lx, ly);
        pixman_region32_intersect(&blur_region, &blur_region, &node->visible_region);
    } else {
        pixman_region32_copy(&blur_region, &node->visible_region);
    }

    pixman_region32_t damaged;
    pixman_region32_init(&damaged);
    pixman_region32_intersect(&damaged, &blur_region, &target->damage);
    bool blur_damaged = pixman_region32_not_empty(&damaged);
    pixman_region32_fini(&damaged);
    if (!blur_damaged) {
        pixman_region32_fini(&blur_region);
        return;
    }

    uint32_t iterations;
    float offset;
    get_blur_level(&node->blur, &iterations, &offset);
    int distance = calculate_blur_radius(iterations, offset);
    distance = ceil(distance / target->scale);

    /* blur expand region, region for blur */
    wlr_region_expand(&blur_region, &blur_region, distance);
    pixman_region32_union(half_expand_damage, &blur_region, half_expand_damage);
    /* region for copyback and blur */
    wlr_region_expand(&blur_region, &blur_region, distance);
    /* blur expand region add to damage */
    pixman_region32_union(&target->damage, &blur_region, &target->damage);
    /* blur region in damage, include expand region */
    pixman_region32_union(damage_blur, damage_blur, &blur_region);
    pixman_region32_fini(&blur_region);
}

static bool blur_frame_render_begin(struct effect_entity *entity,
                                    struct ky_scene_render_target *target)
{
    struct blur_data *data = entity->user_data;

    struct ky_opengl_render_pass *gl_pass =
        ky_opengl_render_pass_from_wlr_render_pass(target->render_pass);
    struct wlr_texture *src_tex =
        ky_opengl_texture_from_buffer(&blur_config.renderer->wlr_renderer, gl_pass->buffer->buffer);
    data->current_output_texture = ky_opengl_texture_from_wlr_texture(src_tex);

    struct blur_output_data *output_data = NULL, *exits_data;
    wl_list_for_each(exits_data, &data->output_data, link) {
        if (exits_data->output == target->output) {
            output_data = exits_data;
            break;
        }
    }
    if (!output_data) {
        output_data = calloc(1, sizeof(*output_data));
        if (!output_data) {
            data->current_output_data = NULL;
            return true;
        }

        wl_list_insert(&data->output_data, &output_data->link);
        pixman_region32_init(&output_data->unaffected_region);
        pixman_region32_init(&output_data->damage_blur_region);
        output_data->output = target->output;
        output_data->output_destroy.notify = handle_output_destroy;
        wl_signal_add(&target->output->events.destroy, &output_data->output_destroy);
    }
    data->current_output_data = output_data;

    pixman_region32_clear(&output_data->unaffected_region);
    pixman_region32_clear(&output_data->damage_blur_region);

    pixman_region32_t half_expand_damage;
    pixman_region32_init(&half_expand_damage);
    pixman_region32_copy(&half_expand_damage, &target->damage);

    struct ky_scene_node *node = &data->effect->scene->tree.node;
    node_for_each_blur_region(node, target, 0, 0, data, &half_expand_damage);

    /* the target must be output in render begin */
    pixman_region32_intersect_rect(&target->damage, &target->damage, target->logical.x,
                                   target->logical.y, target->logical.width,
                                   target->logical.height);
    pixman_region32_subtract(&output_data->unaffected_region, &target->damage, &half_expand_damage);

    pixman_region32_fini(&half_expand_damage);
    return true;
}

static bool blur_frame_render_end(struct effect_entity *entity,
                                  struct ky_scene_render_target *target)
{
    struct blur_data *data = entity->user_data;

    if (data->current_output_texture) {
        wlr_texture_destroy(&data->current_output_texture->wlr_texture);
        data->current_output_texture = NULL;
    }

    struct blur_output_data *output_data = data->current_output_data;
    if (!output_data || !output_data->output_buffer ||
        !pixman_region32_not_empty(&output_data->unaffected_region)) {
        return true;
    }

    /**
     * if sub the unaffected region from target damage,
     * the new target damage (to framebuffer coord) union (+) unaffected region (to framebuffer
     * coord) maybe not equal (!=) target old framebuffer damage (to framebuffer coord), when output
     * scale is not integer.
     */
    pixman_region32_union(&target->excluded_damage, &target->excluded_damage,
                          &output_data->unaffected_region);

    pixman_region32_translate(&output_data->unaffected_region, -target->logical.x,
                              -target->logical.y);
    ky_scene_render_region(&output_data->unaffected_region, target);

    KY_PROFILE_RENDER_ZONE(&blur_config.renderer->wlr_renderer, gzone, __func__);
    struct wlr_texture *texture =
        wlr_texture_from_buffer(target->output->output->renderer, output_data->output_buffer);
    struct wlr_render_texture_options options = {
        .texture = texture,
        .clip = &output_data->unaffected_region,
        .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
    };
    wlr_render_pass_add_texture(target->render_pass, &options);
    wlr_texture_destroy(texture);
    KY_PROFILE_RENDER_ZONE_END(&blur_config.renderer->wlr_renderer);

    return true;
}

static void blur_output_data_buffer_release(struct blur_output_data *data)
{
    if (!data->output_buffer) {
        return;
    }
    struct wlr_buffer *buffer = data->output_buffer;
    data->output_buffer = NULL;
    wlr_buffer_unlock(buffer);
}

static void blur_output_data_destroy(struct blur_output_data *data)
{
    wl_list_remove(&data->output_destroy.link);

    blur_output_data_buffer_release(data);

    pixman_region32_fini(&data->unaffected_region);
    pixman_region32_fini(&data->damage_blur_region);
    wl_list_remove(&data->link);
    free(data);
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct blur_output_data *_data = wl_container_of(listener, _data, output_destroy);
    blur_output_data_destroy(_data);
}

static bool blur_frame_render_post(struct effect_entity *entity,
                                   struct ky_scene_render_target *target)
{
    struct blur_data *data = entity->user_data;
    struct wlr_buffer *buffer = target->buffer;
    struct blur_output_data *output_data = NULL, *exits_data;
    wl_list_for_each(exits_data, &data->output_data, link) {
        if (exits_data->output == target->output) {
            output_data = exits_data;
            break;
        }
    }
    if (output_data) {
        if (output_data->output_buffer == buffer) {
            return true;
        }
        blur_output_data_buffer_release(output_data);
        output_data->output_buffer = wlr_buffer_lock(buffer);
        return true;
    }

    /* frame_render_begin creates the per-output state before the first scene
     * render so blur damage propagation is valid during login too. */
    return true;
}

static void handle_effect_enable(struct wl_listener *listener, void *data)
{
    struct blur_effect *effect = wl_container_of(listener, effect, enable);
    ky_scene_damage_whole(effect->scene);
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct blur_effect *effect = wl_container_of(listener, effect, disable);
    ky_scene_damage_whole(effect->scene);
}

static void reset_existing_blur_levels(struct ky_scene_node *node)
{
    if (node->type == KY_SCENE_NODE_TREE) {
        struct ky_scene_tree *tree = ky_scene_tree_from_node(node);
        struct ky_scene_node *child;
        wl_list_for_each(child, &tree->children, link) {
            reset_existing_blur_levels(child);
        }
        return;
    }

    if (node->has_blur) {
        ky_scene_node_reset_blur_level(node);
    }
}

static bool handle_blur_effect_configure(struct effect *effect, const struct effect_option *option)
{
    if (effect_option_is_enabled_option(option)) {
        return true;
    }

    struct blur_effect *blur = effect->user_data;
    if (strcmp(option->key, BLUR_STRENGTH_KEY) == 0) {
        if (option->value.num < MIN_BLUR_STRENGTH || option->value.num > MAX_BLUR_STRENGTH) {
            return false;
        }
        blur->blur_strength = option->value.num;
        reset_existing_blur_levels(&blur->scene->tree.node);
        ky_scene_damage_whole(blur->scene);
        return true;
    }

    if (strcmp(option->key, NOISE_STRENGTH_KEY) == 0) {
        if (option->value.num < MIN_NOISE_STRENGTH || option->value.num > MAX_NOISE_STRENGTH) {
            return false;
        }
        blur->noise_strength = option->value.num;
        ky_scene_damage_whole(blur->scene);
        return true;
    }

    return false;
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct blur_effect *effect = wl_container_of(listener, effect, destroy);
    wl_list_remove(&effect->destroy.link);
    wl_list_remove(&effect->enable.link);
    wl_list_remove(&effect->disable.link);
    blur_data_destroy(&effect_blur_data);
    glrtt_pool_destroy(effect->glrtt_pool);
    free(effect);
}

static const struct effect_interface blur_impl = {
    .frame_render_begin = blur_frame_render_begin,
    .frame_render_end = blur_frame_render_end,
    .frame_render_post = blur_frame_render_post,
    .configure = handle_blur_effect_configure,
};

bool blur_effect_create(struct effect_manager *effect_manager)
{
    /* check blur if opengl renderer support */
    struct wlr_renderer *renderer = effect_manager->server->renderer;
    if (!wlr_renderer_is_opengl(renderer)) {
        blur_config.renderer = NULL;
        return false;
    }
    blur_config.renderer = ky_opengl_renderer_from_wlr_renderer(renderer);

    struct blur_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }
    /* the priority very high, ensure correct display before other effect paint. */
    effect->effect = effect_create("blur", 0, true, &blur_impl, effect);
    if (!effect->effect) {
        free(effect);
        return false;
    }

    effect->glrtt_pool = glrtt_pool_create(GL_RGBA, GL_LINEAR, 3000);
    if (!effect->glrtt_pool) {
        effect_destroy(effect->effect);
        free(effect);
        return false;
    }

    effect->scene = effect_manager->server->scene;
    effect->blur_strength =
        effect_get_option_int(effect->effect, BLUR_STRENGTH_KEY, DEFAULT_BLUR_STRENGTH);
    if (effect->blur_strength < MIN_BLUR_STRENGTH || effect->blur_strength > MAX_BLUR_STRENGTH) {
        effect->blur_strength = DEFAULT_BLUR_STRENGTH;
        effect_set_option_int(effect->effect, BLUR_STRENGTH_KEY, effect->blur_strength);
    }
    effect->noise_strength =
        effect_get_option_int(effect->effect, NOISE_STRENGTH_KEY, DEFAULT_NOISE_STRENGTH);
    if (effect->noise_strength < MIN_NOISE_STRENGTH ||
        effect->noise_strength > MAX_NOISE_STRENGTH) {
        effect->noise_strength = DEFAULT_NOISE_STRENGTH;
        effect_set_option_int(effect->effect, NOISE_STRENGTH_KEY, effect->noise_strength);
    }

    effect->enable.notify = handle_effect_enable;
    wl_signal_add(&effect->effect->events.enable, &effect->enable);
    effect->disable.notify = handle_effect_disable;
    wl_signal_add(&effect->effect->events.disable, &effect->disable);
    effect->destroy.notify = handle_effect_destroy;
    wl_signal_add(&effect->effect->events.destroy, &effect->destroy);

    effect_blur_data.effect = effect;
    wl_list_init(&effect_blur_data.output_data);

    struct effect_entity *entity = ky_scene_add_effect(effect->scene, effect->effect);
    if (!entity) {
        effect_destroy(effect->effect);
        return false;
    }
    entity->user_data = &effect_blur_data;

    return true;
}
