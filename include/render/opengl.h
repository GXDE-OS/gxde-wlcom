// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _RENDER_OPENGL_H_
#define _RENDER_OPENGL_H_

#include <time.h>

#include <epoxy/gl.h>

#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>

#include "egl.h"
#include "pass.h"

struct ky_opengl_rect_ex_shader {
    GLuint program;

    GLint uv2ndc;
    GLint uv2tex;
    GLint uv_attrib;

    GLint color;
    GLint aspect;
    GLint anti_aliasing;
    GLint round_corner_radius;
};

struct ky_opengl_tex_ex_shader {
    GLuint program;

    GLint uv2ndc;
    GLint uv2tex;
    GLint uv_attrib;

    GLint tex;
    GLint alpha;
    GLint force_opaque;
    GLint aspect;
    GLint anti_aliasing;
    GLint round_corner_radius;
};

struct ky_opengl_renderer {
    struct wlr_renderer wlr_renderer;
    bool is_core_profile;

    float projection[9];
    struct ky_egl *egl;
    int drm_fd;

    struct {
        bool EXT_read_format_bgra;
        bool KHR_debug;
        bool OES_egl_image_external;
        bool OES_egl_image;
        bool EXT_texture_type_2_10_10_10_REV;
        bool OES_texture_half_float_linear;
        bool EXT_texture_norm16;
        bool EXT_disjoint_timer_query;
        bool KHR_robustness;
    } exts;

    struct {
        struct ky_opengl_rect_ex_shader quad;
        struct ky_opengl_tex_ex_shader tex_rgba;
        struct ky_opengl_tex_ex_shader tex_rgbx;
        struct ky_opengl_tex_ex_shader tex_ext;
        // round corner clip shader
        struct ky_opengl_rect_ex_shader quad_ex;
        struct ky_opengl_tex_ex_shader tex_rgba_ex;
        struct ky_opengl_tex_ex_shader tex_rgbx_ex;
        struct ky_opengl_tex_ex_shader tex_ext_ex;
    } shaders;

    struct wl_list buffers;  // ky_opengl_buffer.link
    struct wl_list textures; // ky_opengl_texture.link

    struct ky_opengl_buffer *current_buffer;
    uint32_t viewport_width, viewport_height;
};

struct ky_opengl_render_timer {
    struct wlr_render_timer base;
    struct ky_opengl_renderer *renderer;
    struct timespec cpu_start;
    struct timespec cpu_end;
    GLuint id;
    GLint64 gl_cpu_end;
};

struct ky_opengl_buffer {
    struct wlr_buffer *buffer;
    struct ky_opengl_renderer *renderer;
    struct wl_list link; // wlr_gles2_renderer.buffers

    EGLImageKHR image;
    GLuint rbo;
    GLuint fbo;

    struct wlr_addon addon;
};

struct ky_opengl_texture {
    struct wlr_texture wlr_texture;
    struct ky_opengl_renderer *renderer;
    struct wl_list link; // ky_opengl_renderer.textures

    GLenum target;
    GLuint tex;

    EGLImageKHR image;

    bool has_alpha;

    uint32_t drm_format;       // for mutable textures only, used to interpret upload data
    struct wlr_buffer *buffer; // for DMA-BUF and WL-BUF imports only
    struct wlr_addon buffer_addon;
};

struct ky_opengl_render_pass {
    struct wlr_render_pass base;
    const struct ky_render_pass_impl *impl;
    struct ky_opengl_renderer *renderer;
    struct ky_opengl_buffer *buffer;
    float projection_matrix[9];
    struct ky_egl_context prev_ctx;
    struct ky_opengl_render_timer *timer;
};

struct ky_opengl_texture_attribs {
    GLenum target; /* either GL_TEXTURE_2D or GL_TEXTURE_EXTERNAL_OES */
    GLuint tex;

    bool has_alpha;
};

void ky_opengl_matrix_projection(float mat[static 9], int width, int height,
                                 enum wl_output_transform transform);

struct wlr_renderer *ky_opengl_renderer_create_with_drm_fd(int drm_fd);

bool wlr_renderer_is_opengl(struct wlr_renderer *wlr_renderer);

struct ky_opengl_renderer *ky_opengl_renderer_from_wlr_renderer(struct wlr_renderer *wlr_renderer);

struct ky_egl *ky_opengl_renderer_get_egl(struct wlr_renderer *wlr_renderer);

void ky_opengl_pop_debug(struct ky_opengl_renderer *renderer);

void ky_opengl_push_debug_(struct ky_opengl_renderer *renderer, const char *file, const char *func);

#define ky_opengl_push_debug(renderer) ky_opengl_push_debug_(renderer, __FILE__, __func__)

struct ky_opengl_render_pass *ky_opengl_begin_buffer_pass(struct ky_opengl_buffer *buffer,
                                                          struct ky_egl_context *prev_ctx,
                                                          struct ky_opengl_render_timer *timer);

bool wlr_render_pass_is_opengl(struct wlr_render_pass *render_pass);

struct ky_opengl_render_pass *
ky_opengl_render_pass_from_wlr_render_pass(struct wlr_render_pass *wlr_pass);

struct wlr_texture *ky_opengl_texture_from_buffer(struct wlr_renderer *wlr_renderer,
                                                  struct wlr_buffer *buffer);

bool wlr_buffer_is_wayland_buffer(struct wlr_buffer *buffer);

struct wlr_texture *wlr_texture_from_wayland_buffer(struct ky_opengl_renderer *renderer,
                                                    struct wlr_buffer *buffer);

struct ky_opengl_texture *ky_opengl_texture_create(struct ky_opengl_renderer *renderer,
                                                   uint32_t width, uint32_t height);

bool ky_opengl_texture_invalidate(struct ky_opengl_texture *texture);

void ky_opengl_texture_destroy(struct ky_opengl_texture *texture);

struct ky_opengl_texture *ky_opengl_texture_from_wlr_texture(struct wlr_texture *wlr_texture);

bool wlr_texture_is_opengl(struct wlr_texture *wlr_texture);

void ky_opengl_texture_get_attribs(struct wlr_texture *texture,
                                   struct ky_opengl_texture_attribs *attribs);

void ky_opengl_render_pass_add_texture(struct wlr_render_pass *wlr_pass,
                                       const struct ky_render_texture_options *options);

void ky_opengl_render_pass_add_rect(struct wlr_render_pass *wlr_pass,
                                    const struct ky_render_rect_options *options);

GLuint ky_opengl_create_program(struct ky_opengl_renderer *renderer, const GLchar *vert_src,
                                const GLchar *frag_src);

#ifdef TRACY_ENABLE
#include <tracy/TracyC.h>

void ky_profile_gl_create(struct wlr_renderer *renderer);
void ky_profile_gl_destroy(void);
void ky_profile_gl_begin(const struct ___tracy_source_location_data *data);
void ky_profile_gl_end(void);
void ky_profile_gl_collect(void);

#endif /* TRACY_ENABLE */

#endif /* _RENDER_OPENGL_H_ */
