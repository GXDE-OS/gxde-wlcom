// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _SCENE_RENDER_H_
#define _SCENE_RENDER_H_

#include "scene.h"

enum ky_scene_render_option {
    KY_SCENE_RENDER_DISABLE_VISIBILITY = 1 << 0,
    KY_SCENE_RENDER_DISABLE_ROUND_CORNER = 1 << 1,
    KY_SCENE_RENDER_DISABLE_BLUR = 1 << 2,
};

struct ky_scene_render_target {
    /* current output states */
    enum wl_output_transform transform;
    struct wlr_box logical;
    float scale;

    /* transformed output resolution */
    int trans_width, trans_height;

    struct wlr_buffer *buffer;
    struct ky_scene_output *output;
    struct wlr_render_pass *render_pass;

    pixman_region32_t damage;

    /* options when render to this target */
    uint32_t options;
};

/**
 * translate logical coord box to render target buffer coord
 */
void ky_scene_render_box(struct wlr_box *box, struct ky_scene_render_target *target);

void ky_scene_render_region(pixman_region32_t *region, struct ky_scene_render_target *target);

void ky_scene_render_damage_in_target(struct ky_scene *scene,
                                      struct ky_scene_render_target *target);

#endif /* _SCENE_RENDER_H_ */
