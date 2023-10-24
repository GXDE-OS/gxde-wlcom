// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _RENDER_OPENGL_H_
#define _RENDER_OPENGL_H_

#include <time.h>

#include <epoxy/gl.h>

#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>

#include "egl.h"

struct ky_opengl_pixel_format {
    uint32_t drm_format;
    /* Equivalent of the format if it has an alpha channel,
     * DRM_FORMAT_INVALID (0) if NA
     */
    uint32_t opaque_substitute;

    /* Bytes per block (including padding) */
    uint32_t bytes_per_block;
    /* Size of a block in pixels (zero for 1×1) */
    uint32_t block_width, block_height;

    // optional field, if empty then internalformat = format
    GLint gl_internalformat;
    GLint gl_format, gl_type;
    bool has_alpha;
};

struct ky_opengl_tex_shader {
    GLuint program;
    GLint proj;
    GLint tex_proj;
    GLint tex;
    GLint alpha;
    GLint pos_attrib;
};

struct ky_opengl_renderer {
    struct wlr_renderer wlr_renderer;

    float projection[9];
    struct ky_egl *egl;
    int drm_fd;

    const char *exts_str;
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
        struct {
            GLuint program;
            GLint proj;
            GLint color;
            GLint pos_attrib;
        } quad;
        struct ky_opengl_tex_shader tex_rgba;
        struct ky_opengl_tex_shader tex_rgbx;
        struct ky_opengl_tex_shader tex_ext;
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

    // Basically:
    //   GL_TEXTURE_2D == mutable
    //   GL_TEXTURE_EXTERNAL_OES == immutable
    GLenum target;
    GLuint tex;

    EGLImageKHR image;

    bool has_alpha;

    // Only affects target == GL_TEXTURE_2D
    uint32_t drm_format; // used to interpret upload data
    // If imported from a wlr_buffer
    struct wlr_buffer *buffer;
    struct wlr_addon buffer_addon;
};

struct ky_opengl_render_pass {
    struct wlr_render_pass base;
    struct ky_opengl_buffer *buffer;
    float projection_matrix[9];
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

struct ky_opengl_renderer *ky_opengl_renderer_from_wlr_renderer(struct wlr_renderer *wlr_renderer);

struct ky_egl *ky_opengl_renderer_get_egl(struct wlr_renderer *wlr_renderer);

void ky_opengl_pop_debug(struct ky_opengl_renderer *renderer);

void ky_opengl_push_debug_(struct ky_opengl_renderer *renderer, const char *file, const char *func);

#define ky_opengl_push_debug(renderer) ky_opengl_push_debug_(renderer, __FILE__, __func__)

struct ky_opengl_render_pass *ky_opengl_begin_buffer_pass(struct ky_opengl_buffer *buffer,
                                                          struct ky_opengl_render_timer *timer);

struct wlr_texture *ky_opengl_texture_from_buffer(struct wlr_renderer *wlr_renderer,
                                                  struct wlr_buffer *buffer);

void ky_opengl_texture_destroy(struct ky_opengl_texture *texture);

struct ky_opengl_texture *ky_opengl_texture_from_wlr_texture(struct wlr_texture *wlr_texture);

bool wlr_texture_is_opengl(struct wlr_texture *wlr_texture);

void ky_opengl_texture_get_attribs(struct wlr_texture *texture,
                                   struct ky_opengl_texture_attribs *attribs);

bool ky_opengl_pixel_format_is_supported(const struct ky_opengl_renderer *renderer,
                                         const struct ky_opengl_pixel_format *format);

const struct ky_opengl_pixel_format *ky_opengl_pixel_format_from_drm(uint32_t fmt);

const struct ky_opengl_pixel_format *ky_opengl_pixel_format_from_gl(GLint gl_format, GLint gl_type,
                                                                    bool alpha);

const uint32_t *ky_opengl_get_shm_formats(const struct ky_opengl_renderer *renderer, size_t *len);

uint32_t ky_opengl_pixel_format_pixels_per_block(const struct ky_opengl_pixel_format *format);

bool ky_opengl_pixel_format_check_stride(const struct ky_opengl_pixel_format *format,
                                         int32_t stride, int32_t width);

int32_t ky_opengl_pixel_format_min_stride(const struct ky_opengl_pixel_format *format,
                                          int32_t width);

#endif /* _RENDER_OPENGL_H_ */
