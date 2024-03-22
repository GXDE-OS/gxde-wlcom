// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _RENDER_PASS_H_
#define _RENDER_PASS_H_

#include <wlr/render/pass.h>

struct ky_render_round_corner {
    int rb, rt, lb, lt;
};

struct ky_render_texture_options {
    struct wlr_render_texture_options base;
    const pixman_region32_t *blur;
    struct ky_render_round_corner radius;
};

struct ky_render_rect_options {
    struct wlr_render_rect_options base;
    const pixman_region32_t *blur;
    struct ky_render_round_corner radius;
};

struct ky_render_pass_impl {
    void (*add_texture)(struct wlr_render_pass *pass,
                        const struct ky_render_texture_options *options);
    void (*add_rect)(struct wlr_render_pass *pass, const struct ky_render_rect_options *options);
};

void ky_render_pass_add_texture(struct wlr_render_pass *render_pass,
                                const struct ky_render_texture_options *options);

void ky_render_pass_add_rect(struct wlr_render_pass *render_pass,
                             const struct ky_render_rect_options *options);

#endif /* _RENDER_PASS_H_ */
