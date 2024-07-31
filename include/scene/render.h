// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _SCENE_RENDER_H_
#define _SCENE_RENDER_H_

#include "scene.h"

enum ky_scene_render_option {
    KY_SCENE_RENDER_DISABLE_VISIBILITY = 1 << 0,
    KY_SCENE_RENDER_DISABLE_ROUND_CORNER = 1 << 1,
    KY_SCENE_RENDER_DISABLE_BLUR = 1 << 2,
    KY_SCENE_RENDER_DISABLE_EFFECT = 1 << 3,
    KY_SCENE_RENDER_ENABLE_PRESENTATION = 1 << 4,
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

    /* current layout damage in logical coord */
    pixman_region32_t damage;
    /* excluded buffer damage */
    pixman_region32_t excluded_damage;

    /* options when render to this target */
    uint32_t options;
};

struct ky_scene_render_texture_options {
    struct wlr_texture *texture;
    const struct kywc_box *geometry_box;
    /**
     * texture round corner radius
     * 0 = right-bottom, 1 = right-top, 2 = left-bottom, 3 = left-top
     */
    const int (*radius)[4];
    /* the rotation angle is based on transform */
    const float *alpha, angle;
    /* the origin is the texture top-left */
    const struct kywc_fbox *src;
    /* transform applied to the source texture */
    enum wl_output_transform transform;

    /**
     * blur region don't support rotation
     * the origin is the top-left of surface in logical coord
     */
    struct {
        const struct blur_info *info;
        const float *alpha;
        /* the magnification factor of the blur area in the texture */
        const float *scale;
        /**
         * round corner radius on the tex_dst.
         * if blur is in this round corner region, blur don't be rendered.
         * 0 = right-bottom, 1 = right-top, 2 = left-bottom, 3 = left-top
         */
        const int (*radius)[4];
        /* logical coord of the blur area in the texture, not scale */
        int offset_x, offset_y;
    } blur;
};

/**
 * translate logical coord box to render target buffer coord
 */
void ky_scene_render_box(struct wlr_box *box, struct ky_scene_render_target *target);

void ky_scene_render_region(pixman_region32_t *region, struct ky_scene_render_target *target);

void ky_scene_render_damage_in_target(struct ky_scene *scene,
                                      struct ky_scene_render_target *target);

void ky_scene_render_target_add_texture(struct ky_scene_render_target *target,
                                        const struct ky_scene_render_texture_options *opts);

#endif /* _SCENE_RENDER_H_ */
