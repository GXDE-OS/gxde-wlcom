// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _SCENE_P_H_
#define _SCENE_P_H_

#include <kywc/log.h>

#include "scene/scene.h"

void ky_scene_node_init(struct ky_scene_node *node, struct ky_scene_tree *parent);

void ky_scene_rect_init(struct ky_scene_rect *rect, struct ky_scene_tree *parent, int width,
                        int height, const float color[static 4]);

void ky_scene_buffer_init(struct ky_scene_buffer *scene_buffer, struct ky_scene_tree *parent);

/**
 * update output states for buffer node in the tree or the single buffer node, when
 * 1. scene buffer state:
 *      position: set_position, reparent
 *      dest_size
 *      create with buffer
 *      destroy: direct emit output_leave
 * 2. scene output state:
 *      position
 *      mode
 *      create/destroy
 *      scale/transform/subpixel: force update
 */
void ky_scene_node_update_outputs(struct ky_scene_node *node, struct wl_list *outputs,
                                  struct ky_scene_output *ignore, struct ky_scene_output *force);

/**
 * collect current damage and distribute to outputs
 */
void ky_scene_collect_damage(struct ky_scene *scene);

void ky_scene_node_push_damage(struct ky_scene_node *node, enum ky_scene_damage_type damage_type,
                               const pixman_region32_t *damage);

void ky_scene_corner_region(pixman_region32_t *region, int width, int height,
                            const int radius[static 4]);

void ky_scene_log_region(enum kywc_log_level level, const char *desc,
                         const pixman_region32_t *region);

#endif /* _SCENE_P_H_ */
