// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _EFFECT_BLUR_H_
#define _EFFECT_BLUR_H_

#include "render/pass.h"
#include "scene/render.h"

struct blur_render_options {
    int lx, ly;
    const float *alpha;
    const struct wlr_box *dst_box;
    const pixman_region32_t *clip;
    const struct ky_render_round_corner *radius;

    const pixman_region32_t *region;
    uint32_t strength;
};

void blur_render_with_target(struct ky_scene_render_target *target,
                             const struct blur_render_options *options);

#endif /* _EFFECT_BLUR_H_ */
