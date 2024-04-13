// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _DEFAULT_SOURCE
#include <pixman.h>
#include <stdio.h>
#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_matrix.h>

#include <kywc/boxes.h>

#include "blur_tex_rgba_frag.h"
#include "blur_tex_vert.h"
#include "render/opengl.h"
#include "scene/scene.h"
#include "scene/surface.h"
#include "scene_p.h"

struct gl_texture {
    GLenum target;
    GLuint id;
    int32_t width;
    int32_t height;
};

static struct blur_data {
    float offset;
    struct gl_texture *tex[2];
    struct gl_texture *blur_tex;
} blur_tex_data = { 0 };

static struct blur_tex_program {
    GLuint id;

    struct {
        GLint proj;
        GLint tex_proj;
        GLint tex;
        GLint alpha;
        GLint pixel_distance;
        GLint aspect;
        GLint rounded_corner_radius;
        GLint pos_attrib;
        GLint sdfpos_attrib;
    } shaders;
} blur_tex_prog = { 0 };

static struct blur_program {
    GLuint id;

    struct {
        GLint offset;
        GLint position;
        GLint halfpixel;
    } shaders;
} blur_prog[2] = { 0 };

struct blur_render_options {
    int iterations;

    /* render config */
    struct ky_opengl_renderer *renderer;
    struct wlr_texture *frame_copy;
    pixman_region32_t paste_region;
};

static struct blur_render_options blur_options = {
    .iterations = 2,
    .frame_copy = NULL,
    .renderer = NULL,
};

#define MAX_QUADS 86 // 4kb

const char *blur_vertex_source = "attribute vec2 position;"
                                 "varying vec2 uv;"
                                 "void main() {"
                                 "    gl_Position = vec4(position, 0.0, 1.0);"
                                 "    uv = (position.xy + vec2(1.0, 1.0)) * 0.5;"
                                 "}";

const char *up_frag_source =
    "#ifdef GL_ES\r\n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\r\n"
    "precision highp float;\r\n"
    "#else\r\n"
    "precision mediump float;\r\n"
    "#endif\r\n"
    "#endif\r\n"
    "varying vec2 uv;\r\n"
    "uniform float offset;\r\n"
    "uniform sampler2D texture;\r\n"
    "uniform vec2 halfpixel;\r\n"
    "void main()\r\n"
    "{\r\n"
    "    vec4 sum = texture2D(texture, uv + vec2(-halfpixel.x * 2.0, 0.0) * offset);"
    "    sum += texture2D(texture, uv + vec2(-halfpixel.x, halfpixel.y) * offset) * 2.0;"
    "    sum += texture2D(texture, uv + vec2(0.0, halfpixel.y * 2.0) * offset);"
    "    sum += texture2D(texture, uv + vec2(halfpixel.x, halfpixel.y) * offset) * 2.0;"
    "    sum += texture2D(texture, uv + vec2(halfpixel.x * 2.0, 0.0) * offset);"
    "    sum += texture2D(texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset) * 2.0;"
    "    sum += texture2D(texture, uv + vec2(0.0, -halfpixel.y * 2.0) * offset);"
    "    sum += texture2D(texture, uv + vec2(-halfpixel.x, -halfpixel.y) * offset) * 2.0;"

    "    gl_FragColor = sum * 0.0833333333;"
    "}";

const char *down_frag_source =
    "#ifdef GL_ES\r\n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\r\n"
    "precision highp float;\r\n"
    "#else\r\n"
    "precision mediump float;\r\n"
    "#endif\r\n"
    "#endif\r\n"
    "varying vec2 uv;\r\n"
    "uniform float offset;\r\n"
    "uniform sampler2D texture;\r\n"
    "uniform vec2 halfpixel;\r\n"
    "void main()"
    "{"
    "    vec4 sum = texture2D(texture, uv) * 4.0;"

    "    sum += texture2D(texture, uv - halfpixel.xy * offset);"
    "    sum += texture2D(texture, uv + halfpixel.xy * offset);"
    "    sum += texture2D(texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset);"
    "    sum += texture2D(texture, uv - vec2(halfpixel.x, -halfpixel.y) * offset);"

    "    gl_FragColor = sum * 0.125;"
    "}";

static void gl_texture_destroy(struct gl_texture *tex)
{
    glDeleteTextures(1, &tex->id);
    free(tex);
}

static struct gl_texture *gl_texture_create(int width, int height)
{
    struct gl_texture *tex = calloc(1, sizeof(*tex));
    if (!tex) {
        return NULL;
    }

    glGenTextures(1, &tex->id);
    glBindTexture(GL_TEXTURE_2D, tex->id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    tex->target = GL_TEXTURE_2D;
    tex->width = width;
    tex->height = height;

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

static void gl_texture_update(struct gl_texture *tex, int width, int height)
{
    if (tex->width == width && tex->height == height) {
        return;
    }
    if (tex->id <= 0) {
        glGenTextures(1, &tex->id);
    }
    glBindTexture(GL_TEXTURE_2D, tex->id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    tex->target = GL_TEXTURE_2D;
    tex->width = width;
    tex->height = height;
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void gl_texture_copy(struct gl_texture *tex, struct ky_opengl_buffer *src,
                            struct kywc_box *box)
{
    GLint old_fbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
    GLuint new_fbo;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src->fbo);

    glGenFramebuffers(1, &new_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, new_fbo);
    glBindTexture(GL_TEXTURE_2D, tex->id);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex->id, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        kywc_log(KYWC_ERROR, " Failed to copy fbo %d to texture %d.", src->fbo, tex->id);
        return;
    }
    glBlitFramebuffer(box->x, box->y, box->x + box->width, box->y + box->height, 0, 0, box->width,
                      box->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &new_fbo);

    glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);
}

#if 0
static void save_texture_to_rgba(GLuint texture_id, int width, int height, const char *prefix)
{
    char path[50] = { 0 };
    snprintf(path, 50, "/tmp/ky_%s_w%d_h%d_%s", prefix, width, height, ".rgb");
    mkstemps(path, 4);

    int fileszie = width * height * 4;
    void *pixles = calloc(fileszie, sizeof(char));

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    GLuint pbo;
    glGenBuffers(1, &pbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, fileszie, NULL, GL_STREAM_READ);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    /* FBO attach texture */
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_id, 0);
    /**
     * read texture data to PBO.
     * nullptr indicates reading data to PBO, not to cpu memeory.
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

static void push_opengl_debug(void)
{
    ky_opengl_push_debug(blur_options.renderer);
}

static void pop_opengl_debug(void)
{
    ky_opengl_pop_debug(blur_options.renderer);
}

static GLuint compile_shader(const GLchar *source, GLenum type)
{
    push_opengl_debug();
    GLuint shader = glCreateShader(type);

    glShaderSource(shader, 1, &source, NULL);

    int ok;
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (ok == GL_FALSE) {
        char err_info[1024];
        glGetShaderInfoLog(shader, 1024, NULL, err_info);
        kywc_log(KYWC_ERROR, "Failed to load shader, Compiler output:%s", err_info);
        kywc_log(KYWC_INFO, "glsl source: %s", source);
        glDeleteShader(shader);
        pop_opengl_debug();
        return 0;
    }

    pop_opengl_debug();
    return shader;
}

static GLuint opengl_generate_program(const char *vertex_source, const char *frag_source)
{
    push_opengl_debug();
    GLuint vertex_shader = compile_shader(vertex_source, GL_VERTEX_SHADER);
    GLuint fragment_shader = compile_shader(frag_source, GL_FRAGMENT_SHADER);
    if (fragment_shader == 0 || vertex_shader == 0) {
        goto err;
    }

    GLuint result_program = glCreateProgram();
    if (!result_program) {
        goto err;
    }

    glAttachShader(result_program, vertex_shader);
    glAttachShader(result_program, fragment_shader);
    glLinkProgram(result_program);

    glDetachShader(result_program, vertex_shader);
    glDetachShader(result_program, fragment_shader);
    /* Won't be really deleted until program is deleted as well */
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint ok;
    glGetProgramiv(result_program, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        kywc_log(KYWC_ERROR, "Failed to link shader");
        glDeleteProgram(result_program);
        goto err;
    }
    pop_opengl_debug();
    return result_program;

err:
    pop_opengl_debug();
    return 0;
}

static void blur_program_generate(struct blur_program *prog, const char *vertex_source,
                                  const char *frag_source)
{
    GLuint prog_id = opengl_generate_program(vertex_source, frag_source);
    if (prog_id > 0) {
        prog->id = prog_id;
        prog->shaders.position = glGetAttribLocation(prog_id, "position");
        prog->shaders.offset = glGetUniformLocation(prog_id, "offset");
        prog->shaders.halfpixel = glGetUniformLocation(prog_id, "halfpixel");
    }
}

static struct blur_program *blur_get_opengl_program(void)
{
    if (blur_prog[0].id <= 0) {
        blur_program_generate(&blur_prog[0], blur_vertex_source, down_frag_source);
    }

    if (blur_prog[1].id <= 0) {
        blur_program_generate(&blur_prog[1], blur_vertex_source, up_frag_source);
    }

    if (blur_prog[0].id > 0 && blur_prog[1].id > 0) {
        return blur_prog;
    }

    kywc_log(KYWC_ERROR, "Blur shader compile error!");
    return NULL;
}

static struct blur_tex_program *get_blur_text_program(void)
{
    if (blur_tex_prog.id > 0) {
        return &blur_tex_prog;
    }
    GLuint prog = opengl_generate_program(blur_tex_vert, blur_tex_rgba_frag);
    if (prog > 0) {
        blur_tex_prog.id = prog;
        blur_tex_prog.shaders.proj = glGetUniformLocation(prog, "proj");
        blur_tex_prog.shaders.tex_proj = glGetUniformLocation(prog, "tex_proj");
        blur_tex_prog.shaders.tex = glGetUniformLocation(prog, "tex");
        blur_tex_prog.shaders.alpha = glGetUniformLocation(prog, "alpha");
        blur_tex_prog.shaders.pixel_distance = glGetUniformLocation(prog, "pixelDistance");
        blur_tex_prog.shaders.aspect = glGetUniformLocation(prog, "aspect");
        blur_tex_prog.shaders.rounded_corner_radius =
            glGetUniformLocation(prog, "roundedCornerRadius");
        blur_tex_prog.shaders.pos_attrib = glGetAttribLocation(prog, "pos");
        blur_tex_prog.shaders.sdfpos_attrib = glGetAttribLocation(prog, "sdfpos");
        return &blur_tex_prog;
    }
    kywc_log(KYWC_INFO, "Blur shader compile error!");
    return NULL;
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

static void set_tex_matrix(GLint loc, enum wl_output_transform trans, const struct wlr_fbox *box)
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

static void render_iteration(struct gl_texture *in, struct gl_texture *out, int width, int height)
{
    /* Special case for small regions where we can't really blur, because we */
    /* simply have too few pixels */
    width = width < 1 ? 1 : width;
    height = height < 1 ? 1 : height;

    gl_texture_update(out, width, height);

    GLuint out_fbo;
    glGenFramebuffers(1, &out_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, out_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, out->target, out->id, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        kywc_log(KYWC_ERROR, " Failed to create frame buffer.");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    glViewport(0, 0, out->width, out->height);

    glClearColor(1.0f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glBindTexture(GL_TEXTURE_2D, in->id);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

static void blur_fb0(struct blur_data *data)
{
    int iterations = blur_options.iterations;
    float offset = data->offset;

    int width = data->tex[0]->width;
    int height = data->tex[0]->height;

    GLfloat pos_vertex[8] = { -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f };
    GLint old_fbo;
    GLint viewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
    glGetIntegerv(GL_VIEWPORT, viewport);

    struct gl_texture **tex = data->tex;
    struct blur_program *prog = blur_get_opengl_program();
    struct blur_program *down_prog = &prog[0];
    struct blur_program *up_prog = &prog[1];

    glDisable(GL_BLEND);

    glUseProgram(down_prog->id);
    glEnableVertexAttribArray(down_prog->shaders.position);
    glVertexAttribPointer(down_prog->shaders.position, 2, GL_FLOAT, GL_FALSE, 0, pos_vertex);
    glUniform1f(down_prog->shaders.offset, offset);

    int sampleWidth, sampleHeight;
    pixman_region32_t region;
    pixman_region32_init(&region);
    for (int i = 0; i < iterations; i++) {
        sampleWidth = width / (1 << i);
        sampleHeight = height / (1 << i);

        glUniform2f(down_prog->shaders.halfpixel, 0.5f / sampleWidth, 0.5f / sampleHeight);
        render_iteration(tex[i % 2], tex[1 - i % 2], sampleWidth, sampleHeight);
        data->blur_tex = tex[1 - i % 2];
    }
    glDisableVertexAttribArray(down_prog->shaders.position);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);

    glUseProgram(up_prog->id);
    glEnableVertexAttribArray(up_prog->shaders.position);
    glVertexAttribPointer(up_prog->shaders.position, 2, GL_FLOAT, GL_FALSE, 0, pos_vertex);
    glUniform1f(up_prog->shaders.offset, offset);
    for (int i = iterations - 1; i >= 0; i--) {
        sampleWidth = width / (1 << i);
        sampleHeight = height / (1 << i);

        glUniform2f(up_prog->shaders.halfpixel, 0.5f / sampleWidth, 0.5f / sampleHeight);
        render_iteration(tex[1 - i % 2], tex[i % 2], sampleWidth, sampleHeight);
        data->blur_tex = tex[i % 2];
    }
    glDisableVertexAttribArray(up_prog->shaders.position);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    pixman_region32_fini(&region);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

static void render(const struct kywc_box *box, const pixman_region32_t *clip, GLint attrib,
                   const struct wlr_box *sdf_box, GLint sdfpos_attrib)
{
    pixman_region32_t region;
    pixman_region32_init_rect(&region, box->x, box->y, box->width, box->height);

    if (clip) {
        pixman_region32_intersect(&region, &region, clip);
    }

    int rects_len;
    const pixman_box32_t *rects = pixman_region32_rectangles(&region, &rects_len);
    if (rects_len == 0) {
        pixman_region32_fini(&region);
        return;
    }

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

    pixman_region32_fini(&region);
}

static void blur_render(struct ky_opengl_render_pass *pass, const struct wlr_box *dst_box,
                        const pixman_region32_t *clip, const struct ky_render_round_corner *radius,
                        const pixman_region32_t *blur, int blur_strength, float target_scale)
{
    blur_options.renderer = pass->buffer->renderer;

    /* Translucent pos in frame_buffer. */
    pixman_region32_t blur_region;
    pixman_region32_init(&blur_region);
    pixman_region32_copy(&blur_region, blur);
    pixman_region32_intersect(&blur_region, &blur_region, clip);

    pixman_box32_t *cpy_box = pixman_region32_extents(&blur_region);
    struct kywc_box buffer_cpy_box = {
        .x = cpy_box->x1,
        .y = cpy_box->y1,
        .width = cpy_box->x2 - cpy_box->x1,
        .height = cpy_box->y2 - cpy_box->y1,
    };

    if (buffer_cpy_box.width == 0 || buffer_cpy_box.height == 0) {
        return;
    }
    struct gl_texture *texture = gl_texture_create(buffer_cpy_box.width, buffer_cpy_box.height);
    if (!texture) {
        return;
    }

    gl_texture_copy(texture, pass->buffer, &buffer_cpy_box);

    if (blur_tex_data.tex[0]) {
        gl_texture_destroy(blur_tex_data.tex[0]);
    }
    blur_tex_data.tex[0] = texture;

    blur_tex_data.tex[1] = blur_tex_data.tex[1]
                               ? blur_tex_data.tex[1]
                               : gl_texture_create(buffer_cpy_box.width, buffer_cpy_box.height);
    if (!blur_tex_data.tex[1]) {
        return;
    }

    blur_tex_data.blur_tex = NULL;
    if ((int)blur_strength == -1) {
        blur_tex_data.offset = 4.f;
    } else {
        blur_tex_data.offset = blur_strength / 1000.f;
    }

    blur_fb0(&blur_tex_data);
    pixman_region32_fini(&blur_region);

    struct blur_tex_program *prog = get_blur_text_program();
    if (!prog) {
        return;
    }
    glUseProgram(prog->id);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(texture->target, blur_tex_data.blur_tex->id);

    if (target_scale - floor(target_scale) > 0.001) {
        glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else {
        glTexParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }

    struct wlr_fbox src_fbox = { .x = 0, .y = 0, .width = 1.0f, .height = 1.0f };

    glUniform1i(prog->shaders.tex, 0);
    glUniform1f(prog->shaders.alpha, 1.0f);
    set_proj_matrix(prog->shaders.proj, pass->projection_matrix, &buffer_cpy_box);
    set_tex_matrix(prog->shaders.tex_proj, WL_OUTPUT_TRANSFORM_NORMAL, &src_fbox);

    glUniform1f(prog->shaders.aspect, dst_box->width / (float)dst_box->height);
    float half_height = (float)dst_box->height * 0.5f; // shader distance scale
    glUniform1f(prog->shaders.pixel_distance, 1.0 / half_height);
    glUniform4f(prog->shaders.rounded_corner_radius, radius->rb / half_height,
                radius->rt / half_height, radius->lb / half_height, radius->lt / half_height);

    render(&buffer_cpy_box, clip, prog->shaders.pos_attrib, dst_box, prog->shaders.sdfpos_attrib);

    glBindTexture(texture->target, 0);
    glUseProgram(0);
}

void ky_scene_node_render_blur(struct ky_scene_node *node, struct ky_scene_render_target *target,
                               int lx, int ly, const struct wlr_box *dst_box,
                               const pixman_region32_t *clip,
                               const struct ky_render_round_corner *radius)
{
    if (node->has_blur && wlr_render_pass_is_opengl(target->render_pass)) {
        struct ky_opengl_render_pass *gl_pass =
            ky_opengl_render_pass_from_wlr_render_pass(target->render_pass);
        pixman_region32_t blur_region;
        if (pixman_region32_not_empty(&node->blur_region)) {
            pixman_region32_init(&blur_region);
            pixman_region32_copy(&blur_region, &node->blur_region);
        } else {
            struct wlr_surface *surface = wlr_surface_try_from_node(node);
            pixman_region32_init_rect(&blur_region, 0, 0, surface->current.width,
                                      surface->current.height);
        }
        pixman_region32_translate(&blur_region, lx - target->logical.x, ly - target->logical.y);
        ky_scene_render_region(&blur_region, target);

        blur_render(gl_pass, dst_box, clip, radius, &blur_region, node->blur_strength,
                    target->scale);
        pixman_region32_fini(&blur_region);
    }
}
