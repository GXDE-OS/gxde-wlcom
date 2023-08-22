// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <cglm/affine.h>
#include <cglm/cam.h>
#include <epoxy/egl.h>
#include <stdio.h>
#include <string.h>

#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/log.h>

#include "default_common_vert_src.h"
#include "default_tex_frag_src.h"
#include "quad_frag_src.h"
#include "tex_external_frag_src.h"
#include "tex_rgba_frag_src.h"
#include "tex_rgbx_frag_src.h"

#include "scene/kycom/opengl_p.h"

static struct opengl_render {
    struct wlr_egl *egl;
    struct wlr_renderer *wlr_renderer;
    struct shader_text {
        const char *builtin;
        const char *buildin_ext;
        const char *default_common_src;
        const char *default_tex_src;
        const char *quad_src;
        const char *rgba_frag_src;
        const char *rgbx_frag_src;
        const char *external_frag_src;
        const char *external_require;
    } shaders_text;
} _renderer;

static struct opengl_context {
    GLint current_ofb;
} _context;

/***************************kywc_gl_texture**************************************/
static void gl_texture_init_from_buffer(struct kywc_gl_texture *tex, struct wlr_buffer *buffer);

void kywc_gl_texture_destroy(struct kywc_gl_texture *tex)
{
    if (!tex) {
        return;
    }
    if (!tex->wlr_tex) {
        wlr_texture_destroy(tex->wlr_tex);
    }
    /*
     * tex->tex_id isn't generate by kywc_gl_texture_create,
     * does't release in herer.
     */
    free(tex);
}

static void gl_texture_init(struct kywc_gl_texture *tex)
{
    tex->type = 0;
    tex->tex_id = (uint32_t)(-1);
    tex->width = -1;
    tex->height = -1;
    tex->wlr_tex = NULL;
    tex->has_viewport = false;
    memset(&tex->viewport, 0, sizeof(tex->viewport));
}

struct kywc_gl_texture *kywc_gl_texture_create(void)
{
    struct kywc_gl_texture *tex = malloc(sizeof(*tex));
    if (!tex) {
        return NULL;
    }
    gl_texture_init(tex);
    return tex;
}

/* Need destroy */
struct kywc_gl_texture *kywc_gl_texture_from_buffer(struct wlr_buffer *buffer)
{
    if (!buffer) {
        return NULL;
    }

    struct kywc_gl_texture *tex = kywc_gl_texture_create();
    if (!tex) {
        return NULL;
    }
    gl_texture_init_from_buffer(tex, buffer);
    if (tex->tex_id == 0) {
        kywc_gl_texture_destroy(tex);
        return NULL;
    }
    return tex;
}

static struct wlr_texture *buffer_get_texture(struct wlr_buffer *buffer,
                                              struct wlr_renderer *renderer)
{
    struct wlr_client_buffer *client_buffer = wlr_client_buffer_get(buffer);

    return client_buffer ? client_buffer->texture : NULL;
}

static void gl_texture_init_from_buffer(struct kywc_gl_texture *tex, struct wlr_buffer *buffer)
{
    if (!tex) {
        return;
    }
    if (!buffer) {
        gl_texture_init(tex);
        return;
    }
    /* Texture destroyed by wlr_client_buffer. */
    struct wlr_texture *texture_get = buffer_get_texture(buffer, _renderer.wlr_renderer);

    tex->wlr_tex = NULL;
    struct wlr_texture *texture_from = NULL, *texture;
    if (texture_get) {
        texture = texture_get;
    } else {
        /* wlr_texture_destroy must be called when texture unused. */
        texture_from = wlr_texture_from_buffer(_renderer.wlr_renderer, buffer);
        texture = texture_from;
        tex->wlr_tex = texture_from;
    }

    if (!texture || !wlr_texture_is_gles2(texture)) {
        wlr_log(WLR_ERROR, "wlr_texture_from_buffer erro! ptr = %p", texture);
        tex->tex_id = 0;
        return;
    }
    tex->height = texture->height;
    tex->width = texture->width;

    struct wlr_gles2_texture_attribs attribs;
    wlr_gles2_texture_get_attribs(texture, &attribs);

    switch (attribs.target) {
    case GL_TEXTURE_2D:
        tex->type = attribs.has_alpha ? TEXTURE_TYPE_RGBA : TEXTURE_TYPE_RGBX;
        break;
    case GL_TEXTURE_EXTERNAL_OES:
        tex->type = TEXTURE_TYPE_EXTERNAL;
        break;
    default:
        abort();
    }
    tex->target = attribs.target;
    tex->tex_id = attribs.tex;
    tex->has_viewport = false;
}

void kywc_gl_texture_update_src_box(struct kywc_gl_texture *tex, const struct wlr_fbox *src_box)
{
    if (!src_box || wlr_fbox_empty(src_box)) {
        tex->has_viewport = false;
        memset(&tex->viewport, 0, sizeof(tex->viewport));
        return;
    }

    tex->has_viewport = true;
    tex->viewport.x = src_box->x / tex->width;
    tex->viewport.y = src_box->y / tex->height;
    tex->viewport.width = src_box->width / tex->width;
    tex->viewport.height = src_box->height / tex->height;
}

void kywc_gl_texture_nearest(struct kywc_gl_texture *tex)
{
    if (!tex) {
        return;
    }
    glTexParameteri(tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

/***************************opengl function***********************************/
static struct kywc_gl_program color_program, tex_program;

static bool egl_make_current(struct wlr_egl *egl);

static void opengl_init(struct wlr_renderer *renderer)
{
    _renderer.egl = wlr_gles2_renderer_get_egl(renderer);
    _renderer.wlr_renderer = renderer;
    _renderer.shaders_text.builtin = "@builtin@";
    _renderer.shaders_text.buildin_ext = "@builtin_ext@";
    _renderer.shaders_text.default_common_src = default_common_vert_src;
    _renderer.shaders_text.default_tex_src = default_tex_frag_src;
    _renderer.shaders_text.quad_src = quad_frag_src;
    _renderer.shaders_text.rgba_frag_src = tex_rgba_frag_src;
    _renderer.shaders_text.rgbx_frag_src = tex_rgbx_frag_src;
    _renderer.shaders_text.external_frag_src = tex_external_frag_src;
    _renderer.shaders_text.external_require = "#extension GL_OES_EGL_image_external : require\r\n";

    _context.current_ofb = -1;

    wlr_log(WLR_DEBUG, "opengl init start.");
    egl_make_current(_renderer.egl);
    GLuint id = kywc_gl_generate_program(default_common_vert_src, quad_frag_src);

    kywc_gl_program_set(&color_program, id, TEXTURE_TYPE_RGBA);

    kywc_gl_program_compile(&tex_program, default_common_vert_src, default_tex_frag_src);
}

static bool egl_is_current(struct wlr_egl *egl)
{
    return eglGetCurrentContext() == wlr_egl_get_context(egl);
}

static bool egl_make_current(struct wlr_egl *egl)
{
    if (egl_is_current(egl)) {
        return true;
    }
    if (!eglMakeCurrent(wlr_egl_get_display(egl), EGL_NO_SURFACE, EGL_NO_SURFACE,
                        wlr_egl_get_context(egl))) {
        wlr_log(WLR_ERROR, "eglMakeCurrent failed");
        return false;
    }

    return true;
}

static void push_opengl_debug(void)
{
    const char *file = __FILE__;
    const char *func = __func__;
    int len = snprintf(NULL, 0, "%s:%s", file, func) + 1;
    char str[len];
    snprintf(str, len, "%s:%s", file, func);
    glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION_KHR, 1, -1, str);
}

static void pop_opengl_debug(void)
{
    glPopDebugGroupKHR();
}

static bool opengl_begin(uint32_t width, uint32_t height, uint32_t fb)
{
    egl_make_current(_renderer.egl);
    push_opengl_debug();
    GLenum status = glGetGraphicsResetStatusKHR();
    if (status != GL_NO_ERROR) {
        wlr_log(WLR_ERROR, "GPU reset (%d)", status);
        return false;
    }

    if (fb != 0 && fb != (uint32_t)(-1)) {
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
    }
    if ((width != 0 && width != (uint32_t)(-1)) || (height != 0 && height != (uint32_t)(-1))) {
        glViewport(0, 0, width, height);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    pop_opengl_debug();

    return true;
}

static void opengl_end(uint32_t fb)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
}

void _kywc_gl_update_current_framebuffer(void)
{
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &_context.current_ofb);
}

GLint kywc_gl_get_current_framebuffer(void)
{
    return _context.current_ofb;
}

/**********************global opengl function*********************************/
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
        wlr_log(WLR_ERROR, "Failed to load shader, Compiler output:%s", err_info);
        wlr_log(WLR_INFO, "glsl source: %s", source);
        glDeleteShader(shader);
        pop_opengl_debug();
        return 0;
    }

    pop_opengl_debug();
    return shader;
}

GLuint kywc_gl_generate_program(const char *vertex_source, const char *frag_source)
{
    push_opengl_debug();
    egl_make_current(_renderer.egl);
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
        wlr_log(WLR_ERROR, "Failed to link shader");
        glDeleteProgram(result_program);
        goto err;
    }
    pop_opengl_debug();
    return result_program;

err:
    pop_opengl_debug();
    return 0;
}

static GLint attrib_pointer(GLuint program_id, const char *name, int size, int stride,
                            const void *ptr, GLenum type)
{
    GLint loc = glGetAttribLocation(program_id, name);
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, size, type, GL_FALSE, stride, ptr);
    return loc;
}

#if 0
static GLint attrib_divisor(GLuint program_id, const char *name, int divisor)
{
    GLint loc = glGetAttribLocation(program_id, name);
    glVertexAttribDivisor(loc, divisor);
    return loc;
}
#endif

static void uniform_matrix4f(GLuint program_id, const char *name, mat4 mat)
{
    GLint loc = glGetUniformLocation(program_id, name);
    glUniformMatrix4fv(loc, 1, GL_FALSE, &mat[0][0]);
}

static void uniform1i(GLuint program_id, const char *name, GLint v0)
{
    GLint loc = glGetUniformLocation(program_id, name);
    glUniform1i(loc, v0);
}

static void uniform1f(GLuint program_id, const char *name, GLfloat v0)
{
    GLint loc = glGetUniformLocation(program_id, name);
    glUniform1f(loc, v0);
}

static void uniform2f(GLuint program_id, const char *name, GLfloat v0, GLfloat v1)
{
    GLint loc = glGetUniformLocation(program_id, name);
    glUniform2f(loc, v0, v1);
}

#if 0
static void uniform3f(GLuint program_id, const char *name, GLfloat v0, GLfloat v1, GLfloat v2)
{
    GLint loc = glGetUniformLocation(program_id, name);
    glUniform3f(loc, v0, v1, v2);
}
#endif

static void uniform4f(GLuint program_id, const char *name, GLfloat v0, GLfloat v1, GLfloat v2,
                      GLfloat v3)
{
    GLint loc = glGetUniformLocation(program_id, name);
    glUniform4f(loc, v0, v1, v2, v3);
}

static void set_active_texture(GLuint program_id, struct kywc_gl_texture *texture)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(texture->target, texture->tex_id);
    glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    vec2 base = { 0.0f, 0.0f };
    vec2 scale = { 1.0f, 1.0f };

    if (texture->has_viewport) {
        scale[0] = texture->viewport.width;
        scale[1] = texture->viewport.height;
        base[0] = texture->viewport.x;
        base[1] = texture->viewport.y;
    }

    uniform2f(program_id, "uv2_base", base[0], base[1]);
    uniform2f(program_id, "uv2_scale", scale[0], scale[1]);
}

static void geomtery_to_texture_vertex_array(GLfloat vertex[8], GLfloat x1, GLfloat y1, GLfloat x2,
                                             GLfloat y2)
{
    /**
     * start at texture left bottom
     * counter-clockwise direction
     * GLfloat vertexData[] = {
     *     x1, y1,
     *     x2, y1,
     *     x2, y2,
     *     x1, y2,
     * };
     */
    vertex[0] = x1, vertex[1] = y1;
    vertex[2] = x2, vertex[3] = y1;
    vertex[4] = x2, vertex[5] = y2;
    vertex[6] = x1, vertex[7] = y2;
}

static void geomtery_to_pos_vertex_array(GLfloat vertex[8], GLfloat x1, GLfloat y1, GLfloat x2,
                                         GLfloat y2)
{
    /**
     * geometry vertex
     * start at texture left bottom
     * counter-clockwise direction
     * GLfloat vertexData[] = {
     *     x1, y2,
     *     x2, y2,
     *     x2, y1,
     *     x1, y1,
     * };
     */
    vertex[0] = x1, vertex[1] = y2;
    vertex[2] = x2, vertex[3] = y2;
    vertex[4] = x2, vertex[5] = y1;
    vertex[6] = x1, vertex[7] = y1;
}

static void save_rgb(char *file_path, char *data, int data_size)
{
    FILE *f = fopen(file_path, "wb+");

    if (f) {
        int pixnum = data_size / 4;
        for (int i = 0; i < pixnum; i++) {
            fwrite(data + i * 4, 4, 1, f);
        }
        fclose(f);
    }
}

static void save_framebuffer_to_rgb(char *file_path, int width, int height)
{
    int fileszie = width * height * 4;
    void *pixles = calloc(fileszie, sizeof(char));
    memset(pixles, 0, fileszie);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixles);
    save_rgb(file_path, pixles, fileszie);
    free(pixles);
}

/***************************opengl program***********************************/
void _kywc_gl_init(struct wlr_renderer *renderer)
{
    opengl_init(renderer);
}

void kywc_gl_push_debug(void)
{
    push_opengl_debug();
}

void kywc_gl_pop_debug(void)
{
    pop_opengl_debug();
}

bool kywc_gl_begin(void)
{
    return opengl_begin(-1, -1, -1);
}

void kywc_gl_end(void)
{
    opengl_end(kywc_gl_get_current_framebuffer());
}

bool _kywc_gl_begin(uint32_t width, uint32_t height, uint32_t fb)
{
    return opengl_begin(width, height, fb);
}

void _kywc_gl_end(uint32_t fb)
{
    opengl_end(fb);
}

void kywc_gl_program_delete(struct kywc_gl_program *program)
{
    if (!program) {
        return;
    }
    for (int i = 0; i < TEXTURE_TYPES_ALL; i++) {
        if (program->program_id[i]) {
            glDeleteProgram(program->program_id[i]);
            program->program_id[i] = 0;
        }
    }
}

bool kywc_gl_program_compile(struct kywc_gl_program *program, const char *vertex_source,
                             const char *frag_source)
{
    if (!program || !vertex_source || !frag_source) {
        return false;
    }
    const char *new_frag_source = NULL;

    GLuint program_id;
    for (int i = 0; i < TEXTURE_TYPES_ALL; i++) {
        new_frag_source = kywc_gl_generate_fragment_shader(frag_source, i);
        program_id = kywc_gl_generate_program(vertex_source, new_frag_source);
        if (program_id > 0) {
            program->program_id[i] = program_id;
        }
        if (new_frag_source) {
            free((char *)new_frag_source);
        }
    }

    return true;
}

bool kywc_gl_program_is_compiled(struct kywc_gl_program *program)
{
    if (!program) {
        return false;
    }
    for (int i = 0; i < TEXTURE_TYPES_ALL; i++) {
        if (program->program_id[i] <= 0) {
            return false;
        }
    }

    return true;
}

void kywc_gl_program_set_active_texture(struct kywc_gl_program *program,
                                        struct kywc_gl_texture *texture)
{
    if (!program || program->active.program_id <= 0 || !texture) {
        return;
    }

    set_active_texture(program->active.program_id, texture);
}

bool kywc_gl_program_set(struct kywc_gl_program *program, GLuint program_id,
                         enum kywc_texture_type tex_type)
{
    if (tex_type >= TEXTURE_TYPES_ALL || tex_type < 0) {
        return false;
    }

    program->program_id[tex_type] = program_id;

    return true;
}

void kywc_gl_program_use(struct kywc_gl_program *program, enum kywc_texture_type type)
{
    if (!program || type < 0 || type >= TEXTURE_TYPES_ALL) {
        return;
    }

    if (program->program_id[type] == 0) {
        wlr_log(WLR_ERROR, "Texture type: %d, gl program link error.", type);
        return;
    }
    program->active.program_id = program->program_id[type];
    glUseProgram(program->program_id[type]);
}

/* Must free the result if result is non-NULL. */
static char *str_replace(char *orig, const char *rep, const char *with)
{
    char *result, *ins, *tmp;
    int len_rep, len_with, len_front;
    int count;

    if (!orig || !rep) {
        return NULL;
    }
    len_rep = strlen(rep);
    if (len_rep == 0) {
        return NULL;
    }
    if (!with) {
        with = "";
    }
    len_with = strlen(with);

    ins = orig;
    for (count = 0; (tmp = strstr(ins, rep)) != NULL; ++count) {
        ins = tmp + len_rep;
    }

    result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);
    tmp = result;

    if (!result) {
        return NULL;
    }

    /**
     * first time through the loop, all the variable are set correctly
     * from here on,
     * tmp points to the end of the result string
     * ins points to the next occurrence of rep in orig
     * orig points to the remainder of orig after "end of rep"
     */
    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep;
    }
    strcpy(tmp, orig);
    return result;
}

const char *kywc_gl_generate_fragment_shader(const char *frag_source, enum kywc_texture_type type)
{
    if (!frag_source) {
        return NULL;
    }

    int frag_len = strlen(frag_source);

    char *new_frag_source = NULL;
    char *frag_source_copy = malloc(frag_len + 1);
    if (!frag_source_copy) {
        return NULL;
    }
    strncpy(frag_source_copy, frag_source, frag_len);
    frag_source_copy[frag_len] = '\0';

    char *temp = NULL;
    switch (type) {
    case TEXTURE_TYPE_RGBA:
        temp = str_replace(frag_source_copy, _renderer.shaders_text.buildin_ext, "");
        new_frag_source =
            str_replace(temp, _renderer.shaders_text.builtin, _renderer.shaders_text.rgba_frag_src);
        break;
    case TEXTURE_TYPE_RGBX:
        temp = str_replace(frag_source_copy, _renderer.shaders_text.buildin_ext, "");
        new_frag_source =
            str_replace(temp, _renderer.shaders_text.builtin, _renderer.shaders_text.rgbx_frag_src);
        break;
    case TEXTURE_TYPE_EXTERNAL:
        temp = str_replace(frag_source_copy, _renderer.shaders_text.builtin,
                           _renderer.shaders_text.external_frag_src);
        new_frag_source = str_replace(temp, _renderer.shaders_text.buildin_ext,
                                      _renderer.shaders_text.external_require);
        break;
    default:
        break;
    }
    if (temp) {
        free(temp);
    }
    free(frag_source_copy);
    return new_frag_source;
}

/* Memory leak: attrib_pointer_id realase */
void kywc_gl_program_attrib_pointer(struct kywc_gl_program *program, const char *name, int size,
                                    int stride, const void *ptr, GLenum type)
{
    if (!program || program->active.program_id <= 0) {
        return;
    }

    program->active.attrib_pointer_id =
        attrib_pointer(program->active.program_id, name, size, stride, ptr, type);
}

void kywc_gl_program_uniform_matrix4f(struct kywc_gl_program *program, const char *name, mat4 mat)
{
    if (!program || program->active.program_id <= 0) {
        return;
    }

    uniform_matrix4f(program->active.program_id, name, mat);
}

void kywc_gl_program_uniform4f(struct kywc_gl_program *program, const char *name, GLfloat v0,
                               GLfloat v1, GLfloat v2, GLfloat v3)
{
    if (!program || program->active.program_id <= 0) {
        return;
    }
    uniform4f(program->active.program_id, name, v0, v1, v2, v3);
}

void kywc_gl_program_uniform2f(struct kywc_gl_program *program, const char *name, GLfloat v0,
                               GLfloat v1)
{
    if (!program || program->active.program_id <= 0) {
        return;
    }
    uniform2f(program->active.program_id, name, v0, v1);
}

void kywc_gl_program_uniform1f(struct kywc_gl_program *program, const char *name, GLfloat v0)
{
    if (!program || program->active.program_id <= 0) {
        return;
    }
    uniform1f(program->active.program_id, name, v0);
}

void kywc_gl_program_uniform1i(struct kywc_gl_program *program, const char *name, GLint v0)
{
    if (!program || program->active.program_id <= 0) {
        return;
    }
    uniform1i(program->active.program_id, name, v0);
}

void kywc_gl_program_deactive(struct kywc_gl_program *program)
{
    if (!program) {
        return;
    }
    if (program->active.attrib_divisor_id > 0) {
        glVertexAttribDivisor(program->active.attrib_divisor_id, 0);
        program->active.attrib_divisor_id = 0;
    }
    if (program->active.attrib_pointer_id > 0) {
        glDisableVertexAttribArray(program->active.attrib_pointer_id);
        program->active.attrib_pointer_id = 0;
    }

    glUseProgram(0);
}

/**********************render opengl function**********************************/
void kywc_gl_framebuffer_to_rgb(const char *path, int frame_num, int width, int height)
{
    char filepath[1024] = { 0 };
    strncpy(filepath, path, 1024);
    snprintf(filepath + strlen(filepath), 1024, "wlcom_frame_%d.rgb", frame_num);
    save_framebuffer_to_rgb(filepath, width, height);
}

void kywc_gl_render_clear_cached(void)
{
    glDisable(GL_BLEND);
    kywc_gl_program_deactive(&tex_program);
    pop_opengl_debug();
}

void kywc_gl_render_rectangle(struct wlr_box *geometry, vec4 color, mat4 matrix,
                              uint32_t cache_flag)
{
    push_opengl_debug();
    kywc_gl_program_use(&color_program, TEXTURE_TYPE_RGBA);
    float x = geometry->x, y = geometry->y, w = geometry->width, h = geometry->height;

    GLfloat vertexData[] = {
        x, y + h, x + w, y + h, x + w, y, x, y,
    };
    for (int i = 0; i < 8; i++) {
        tex_program.pos_vertex[i] = vertexData[i];
    }

    kywc_gl_program_attrib_pointer(&color_program, "v2_pos", 2, 0, tex_program.pos_vertex,
                                   GL_FLOAT);
    kywc_gl_program_uniform_matrix4f(&color_program, "MVP", matrix);
    kywc_gl_program_uniform4f(&color_program, "uv4_color", color[0], color[1], color[2], color[3]);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (cache_flag == RENDER_FLAG_CACHED) {
        return;
    }
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    kywc_gl_render_clear_cached();
}

void kywc_gl_render_texture(struct kywc_gl_texture *tex, const struct kywc_gl_geometry *vertex,
                            const struct kywc_gl_geometry *tex_coord, mat4 mvp, vec4 color,
                            uint32_t flag)
{
    push_opengl_debug();
    kywc_gl_program_use(&tex_program, tex->type);
    const GLfloat x1 = vertex->x1;
    const GLfloat y1 = vertex->y1;
    const GLfloat x2 = vertex->x2;
    const GLfloat y2 = vertex->y2;

    geomtery_to_pos_vertex_array(tex_program.pos_vertex, x1, y1, x2, y2);

    geomtery_to_texture_vertex_array(tex_program.texture_vertex, tex_coord->x1, tex_coord->y1,
                                     tex_coord->x2, tex_coord->y2);

    kywc_gl_program_set_active_texture(&tex_program, tex);
    kywc_gl_program_attrib_pointer(&tex_program, "v2_pos", 2, 0, tex_program.pos_vertex, GL_FLOAT);
    kywc_gl_program_attrib_pointer(&tex_program, "v2_texcoord", 2, 0, tex_program.texture_vertex,
                                   GL_FLOAT);
    kywc_gl_program_uniform_matrix4f(&tex_program, "MVP", mvp);
    kywc_gl_program_uniform4f(&tex_program, "uv4_color", color[0], color[1], color[2], color[3]);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    if (flag == RENDER_FLAG_CACHED) {
        return;
    }
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    kywc_gl_render_clear_cached();
}

void kywc_gl_render_transformed_texture(struct kywc_gl_texture *tex, GLfloat pos_vertex[8],
                                        GLfloat texture_vertex[8], mat4 mvp, vec4 color,
                                        uint32_t flag)
{
    for (int i = 0; i < 8; i++) {
        tex_program.pos_vertex[i] = pos_vertex[i];
        tex_program.texture_vertex[i] = texture_vertex[i];
    }
    push_opengl_debug();
    kywc_gl_program_use(&tex_program, tex->type);

    kywc_gl_program_set_active_texture(&tex_program, tex);
    kywc_gl_program_attrib_pointer(&tex_program, "v2_pos", 2, 0, tex_program.pos_vertex, GL_FLOAT);
    kywc_gl_program_attrib_pointer(&tex_program, "v2_texcoord", 2, 0, tex_program.texture_vertex,
                                   GL_FLOAT);
    kywc_gl_program_uniform_matrix4f(&tex_program, "MVP", mvp);
    kywc_gl_program_uniform4f(&tex_program, "uv4_color", color[0], color[1], color[2], color[3]);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    if (flag == RENDER_FLAG_CACHED) {
        return;
    }
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    kywc_gl_render_clear_cached();
}

/***************************kywc_gl_buffer function***********************************/
void kywc_gl_buffer_bind(struct kywc_gl_buffer *buffer)
{
    if (!buffer) {
        return;
    }
    _kywc_gl_begin(buffer->width, buffer->height, buffer->fb);
}

void kywc_gl_buffer_unbind(int current_ofb)
{
    _kywc_gl_end(current_ofb);
}

void kywc_gl_buffer_init(struct kywc_gl_buffer *buffer)
{
    if (!buffer) {
        return;
    }

    buffer->fb = (uint32_t)(-1);

    buffer->fb_tex = NULL;
    buffer->width = 0;
    buffer->height = 0;
}

bool kywc_gl_buffer_allocate(struct kywc_gl_buffer *buffer, GLuint current_ofb, int width,
                             int height)
{
    bool first_allocate = false;
    if (buffer->fb == (uint32_t)(-1) || buffer->fb == 0) {
        first_allocate = true;

        glGenFramebuffers(1, &buffer->fb);
    }

    if (!buffer->fb_tex || buffer->fb_tex->tex_id == (uint32_t)(-1) ||
        buffer->fb_tex->tex_id == 0) {
        first_allocate = true;
        if (!buffer->fb_tex) {
            buffer->fb_tex = kywc_gl_texture_create();
            if (!buffer->fb_tex) {
                return false;
            }
        }

        buffer->fb_tex->target = GL_TEXTURE_2D;
        glGenTextures(1, &buffer->fb_tex->tex_id);
        glBindTexture(GL_TEXTURE_2D, buffer->fb_tex->tex_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        buffer->fb_tex->type = TEXTURE_TYPE_RGBA;
        buffer->fb_tex->has_viewport = false;
        buffer->fb_tex->height = height;
        buffer->fb_tex->width = width;
    }

    bool is_resize = false;
    /* Special case: fb = 0. This occurs in the default workspace streams,
     * we don't resize anything.
     */
    if (buffer->fb != current_ofb) {
        if (first_allocate || (width != buffer->width) || (height != buffer->height)) {
            is_resize = true;
            glBindTexture(GL_TEXTURE_2D, buffer->fb_tex->tex_id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
            buffer->fb_tex->type = TEXTURE_TYPE_RGBA;
            buffer->fb_tex->has_viewport = false;
            buffer->fb_tex->height = height;
            buffer->fb_tex->width = width;
        }
    }

    if (first_allocate) {
        glBindFramebuffer(GL_FRAMEBUFFER, buffer->fb);
        glBindTexture(GL_TEXTURE_2D, buffer->fb_tex->tex_id);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               buffer->fb_tex->tex_id, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            wlr_log(WLR_ERROR, " Failed to create frame buffer.");
            return false;
        }
    }

    buffer->width = width;
    buffer->height = height;

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, current_ofb);

    return is_resize || first_allocate;
}

void kywc_gl_buffer_release(struct kywc_gl_buffer *buffer)
{
    GLuint fb = buffer->fb;
    if ((fb != (uint32_t)(-1)) && (fb != 0)) {
        glDeleteFramebuffers(1, &fb);
    }

    if (buffer->fb_tex) {
        GLuint tex = buffer->fb_tex->tex_id;
        if ((tex != (uint32_t)(-1)) && ((fb != 0) || (tex != 0))) {
            glDeleteTextures(1, &tex);
        }
        kywc_gl_texture_destroy(buffer->fb_tex);
    }

    kywc_gl_buffer_init(buffer);
}

void kywc_gl_buffer_draw_region(struct kywc_gl_buffer *buffer, pixman_region32_t *region)
{
    if (!buffer->fb_tex) {
        return;
    }
    glBindTexture(buffer->fb_tex->target, buffer->fb_tex->tex_id);

    if (!region) {
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    struct wlr_box scissor_box;
    int nrects;
    pixman_box32_t *rects = pixman_region32_rectangles(region, &nrects);
    for (int i = 0; i < nrects; ++i) {
        scissor_box.x = rects[i].x1;
        scissor_box.y = rects[i].y1;
        scissor_box.width = rects[i].x2 - rects[i].x1;
        scissor_box.height = rects[i].y2 - rects[i].y1;

        glScissor(scissor_box.x, scissor_box.y, scissor_box.width, scissor_box.height);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    glDisable(GL_SCISSOR_TEST);
}

void kywc_gl_buffer_blit_box(struct kywc_gl_buffer *dst, const struct kywc_gl_buffer *src,
                             const struct wlr_box *src_box, const struct wlr_box *dst_box)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src->fb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst->fb);
    glBlitFramebuffer(src_box->x, src_box->y, src_box->x + src_box->width,
                      src_box->y + src_box->height, dst_box->x, dst_box->y,
                      dst_box->x + dst_box->width, dst_box->y + dst_box->height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
}
