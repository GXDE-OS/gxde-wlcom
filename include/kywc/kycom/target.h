// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _KYCOM_TARGET_H_
#define _KYCOM_TARGET_H_

#include <pixman.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#include "opengl.h"

struct kywc_scene_output;

struct kywc_render_target {
    struct kywc_gl_buffer buffer;
    /* Current output bind's framebuffer. */
    int current_ofb;
    /* Need scale in framebuffer, output->scale. */
    float scale;
    /* Logic coord, view box in current ofb. */
    struct wlr_box view_box;
    /* Output transform. */
    enum wl_output_transform wl_transform;
    /* Logic coord,target start pos(lx,ly) in scene. */
    int lx, ly;

    mat4 output_transform_mat;
};

void kywc_target_init_output_target(struct kywc_render_target *target,
                                    struct kywc_scene_output *scene_output);

void kywc_target_cpy(struct kywc_render_target *dst, const struct kywc_render_target *src);

void kywc_target_cpy_offset(struct kywc_render_target *dst, const struct kywc_render_target *src,
                            int x, int y);

void kywc_target_offset(struct kywc_render_target *target, int x, int y);

void kywc_target_offset_revert(struct kywc_render_target *target, int x, int y);

void kywc_target_render_begin(struct kywc_render_target *target);

void kywc_target_render_end(struct kywc_render_target *target);

void kywc_target_update_ofb(struct kywc_render_target *target);

bool kywc_target_get_transform_mat4(struct kywc_render_target *target, mat4 transform_matrix);

void kywc_target_get_framebuffer_box(struct kywc_render_target *target,
                                     const struct wlr_box *geometry_box,
                                     struct wlr_box *framebuffer_box);

void kywc_target_get_frame_region(struct kywc_render_target *target,
                                  const pixman_region32_t *logic_region,
                                  pixman_region32_t *framebuffer_region);

void kywc_target_cpy_framebuffer_box(struct kywc_gl_buffer *dst, struct kywc_render_target *src,
                                     struct wlr_box *cpy_box);

void kywc_target_get_orth(struct kywc_render_target *framebuffer, mat4 transform_mat4,
                          mat4 ortho_proj_mat4);

void kywc_target_logic_damage_clear(struct kywc_render_target *target,
                                    pixman_region32_t *logic_damage);

void kywc_target_draw_damage(struct kywc_render_target *target, struct kywc_gl_texture *tex,
                             pixman_region32_t *damage);

void kywc_target_render_rectangle(struct kywc_render_target *target, struct wlr_box *geometry,
                                  vec4 color, uint32_t cache_flag);

void kywc_target_render_texture_with_transform(struct kywc_gl_texture *texture,
                                               enum kywc_gl_transform transform,
                                               struct kywc_render_target *framebuffer,
                                               const struct kywc_gl_geometry *geometry, vec4 color,
                                               uint32_t cache_flag);

#endif
