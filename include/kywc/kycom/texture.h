// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _KYCOM_TEXTURE_H_
#define _KYCOM_TEXTURE_H_

#include "scene.h"

struct kywc_texture_node *kywc_texture_node_from_node(struct kywc_node *node);

struct kywc_texture_node *kywc_texture_node_create(struct kywc_group_node *parent,
                                                   struct kywc_gl_texture *texture);

void kywc_texture_node_init(struct kywc_texture_node *texture_node);

struct kywc_texture_node *kywc_texture_node_create_from_buffer(struct kywc_group_node *parent,
                                                               struct wlr_buffer *buffer);

void kywc_texture_node_set_buffer_with_damage(struct kywc_texture_node *tex_node,
                                              struct wlr_buffer *buffer,
                                              const pixman_region32_t *damage);

void kywc_texture_node_set_texture_with_damage(struct kywc_texture_node *tex_node,
                                               struct kywc_gl_texture *texture,
                                               const pixman_region32_t *damage);

void kywc_texture_node_set_buffer(struct kywc_texture_node *tex_node, struct wlr_buffer *buffer);

void kywc_texture_node_set_texture(struct kywc_texture_node *tex_node,
                                   struct kywc_gl_texture *texture);

void kywc_texture_node_set_opaque_region(struct kywc_texture_node *tex_node,
                                         const pixman_region32_t *region);

void kywc_texture_node_set_corner_region(struct kywc_texture_node *tex_node,
                                         const pixman_region32_t *region);

void kywc_texture_node_set_source_box(struct kywc_texture_node *tex_node,
                                      const struct wlr_fbox *box);

void kywc_texture_node_set_size(struct kywc_texture_node *node, int width, int height);

void kywc_texture_node_set_transform(struct kywc_texture_node *tex_node,
                                     enum wl_output_transform transform);

void kywc_texture_node_send_frame_done(struct kywc_texture_node *tex_node, struct timespec *now);

void kywc_texture_node_update_outputs(struct kywc_texture_node *tex_node,
                                      struct wlr_box *geometry_box, const struct wl_list *outputs,
                                      struct kywc_scene_output *ignore);

void kywc_texture_node_generate_render_task(const struct kywc_node *node, pixman_region32_t *damage,
                                            struct kywc_render_target *target,
                                            struct wl_list *render_tasks);

#endif
