// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _KYCOM_OPENGL_H_
#define _KYCOM_OPENGL_H_

#include <cglm/types.h>
#include <epoxy/gl.h>
#include <pixman.h>

#include <wlr/render/gles2.h>
#include <wlr/util/box.h>

#define RENDER_FLAG_CACHED 1

struct wlr_buffer;

enum kywc_gl_transform {
    TRANSFORM_NORMAL = 0,
    /**
     * 90 degrees counter-clockwise
     */
    TRANSFORM_90 = 1,
    /**
     * 180 degrees counter-clockwise
     */
    TTRANSFORM_180 = 2,
    /**
     * 270 degrees counter-clockwise
     */
    TRANSFORM_270 = 3,
    /**
     * 180 degree flip around a vertical axis
     */
    TRANSFORM_FLIPPED = 4,
    /**
     * flip and rotate 90 degrees counter-clockwise
     */
    TRANSFORM_FLIPPED_90 = 5,
    /**
     * flip and rotate 180 degrees counter-clockwise
     */
    TRANSFORM_FLIPPED_180 = 6,
    /**
     * flip and rotate 270 degrees counter-clockwise
     */
    TRANSFORM_FLIPPED_270 = 7,
};

struct kywc_gl_geometry {
    float x1, y1, x2, y2;
};

enum kywc_texture_type {
    TEXTURE_TYPE_RGBA = 0,
    TEXTURE_TYPE_RGBX,
    TEXTURE_TYPE_EXTERNAL,
    TEXTURE_TYPES_ALL
};

struct kywc_gl_texture {
    enum kywc_texture_type type;
    GLenum target;
    GLuint tex_id;
    int width, height;
    bool has_viewport;
    struct wlr_fbox viewport;
    /* Read only, maybe NULL. */
    struct wlr_texture *wlr_tex;
};

struct kywc_gl_program {
    GLuint program_id[TEXTURE_TYPES_ALL];

    struct {
        GLuint program_id;
        GLint attrib_pointer_id;
        GLint attrib_divisor_id;
    } active;
    GLfloat pos_vertex[8];
    GLfloat texture_vertex[8];
};

struct kywc_gl_buffer {
    GLuint fb;
    int32_t width, height;
    struct kywc_gl_texture *fb_tex;
};

void kywc_gl_push_debug(void);

void kywc_gl_pop_debug(void);

bool kywc_gl_begin(void);

void kywc_gl_end(void);

GLint kywc_gl_get_current_framebuffer(void);

void kywc_gl_framebuffer_to_rgb(const char *path, int frame_num, int width, int height);

const char *kywc_gl_generate_fragment_shader(const char *frag_source, enum kywc_texture_type type);

/***************************gl_program*****************************************/
void kywc_gl_program_delete(struct kywc_gl_program *program);

bool kywc_gl_program_compile(struct kywc_gl_program *program, const char *vertex_source,
                             const char *frag_source);

bool kywc_gl_program_is_compiled(struct kywc_gl_program *program);

void kywc_gl_program_set_active_texture(struct kywc_gl_program *program,
                                        struct kywc_gl_texture *texture);

bool kywc_gl_program_set(struct kywc_gl_program *program, GLuint program_id,
                         enum kywc_texture_type tex_type);

void kywc_gl_program_use(struct kywc_gl_program *program, enum kywc_texture_type type);

GLuint kywc_gl_generate_program(const char *vertex_source, const char *frag_source);

void kywc_gl_program_attrib_pointer(struct kywc_gl_program *program, const char *name, int size,
                                    int stride, const void *ptr, GLenum type);

void kywc_gl_program_uniform_matrix4f(struct kywc_gl_program *program, const char *name, mat4 mat);

void kywc_gl_program_uniform4f(struct kywc_gl_program *program, const char *name, GLfloat v0,
                               GLfloat v1, GLfloat v2, GLfloat v3);

void kywc_gl_program_uniform2f(struct kywc_gl_program *program, const char *name, GLfloat v0,
                               GLfloat v1);

void kywc_gl_program_uniform1f(struct kywc_gl_program *program, const char *name, GLfloat v0);

void kywc_gl_program_uniform1i(struct kywc_gl_program *program, const char *name, GLint v0);

void kywc_gl_program_deactive(struct kywc_gl_program *program);

void kywc_gl_render_texture(struct kywc_gl_texture *tex, const struct kywc_gl_geometry *vertex,
                            const struct kywc_gl_geometry *tex_coord, mat4 mvp, vec4 color,
                            uint32_t flag);

void kywc_gl_render_transformed_texture(struct kywc_gl_texture *tex, GLfloat pos_vertex[8],
                                        GLfloat texture_vertex[8], mat4 mvp, vec4 color,
                                        uint32_t flag);

void kywc_gl_render_rectangle(struct wlr_box *geometry, vec4 color, mat4 matrix,
                              uint32_t cache_flag);

void kywc_gl_render_clear_cached(void);

struct kywc_gl_texture *kywc_gl_texture_create(void);

struct kywc_gl_texture *kywc_gl_texture_from_buffer(struct wlr_buffer *buffer);

void kywc_gl_texture_destroy(struct kywc_gl_texture *tex);

void kywc_gl_texture_update_src_box(struct kywc_gl_texture *tex, const struct wlr_fbox *src_box);

void kywc_gl_texture_nearest(struct kywc_gl_texture *tex);

/***************************gl_buffer*****************************************/
void kywc_gl_buffer_bind(struct kywc_gl_buffer *buffer);

void kywc_gl_buffer_unbind(int current_ofb);

void kywc_gl_buffer_init(struct kywc_gl_buffer *buffer);

bool kywc_gl_buffer_allocate(struct kywc_gl_buffer *buffer, GLuint current_ofb, int width,
                             int height);

void kywc_gl_buffer_release(struct kywc_gl_buffer *buffer);

void kywc_gl_buffer_draw_region(struct kywc_gl_buffer *target, pixman_region32_t *region);

#endif
