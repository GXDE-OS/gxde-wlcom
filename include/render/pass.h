// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _RENDER_PASS_H_
#define _RENDER_PASS_H_

#include <wlr/render/pass.h>

struct ky_render_round_corner {
    float rb, rt, lb, lt;
};

struct ky_render_texture_options {
    struct wlr_render_texture_options base;
    struct ky_render_round_corner radius;
    float rotation_angle;
    bool repeated;
};

struct ky_render_rect_options {
    struct wlr_render_rect_options base;
    struct ky_render_round_corner radius;
    float rotation_angle;
};

struct ky_render_pass_impl {
    bool (*submit)(struct wlr_render_pass *pass, uint32_t quirks);
    void (*add_texture)(struct wlr_render_pass *pass,
                        const struct ky_render_texture_options *options);
    void (*add_rect)(struct wlr_render_pass *pass, const struct ky_render_rect_options *options);
};

bool ky_render_pass_submit(struct wlr_render_pass *render_pass, uint32_t quirks);

void ky_render_pass_add_texture(struct wlr_render_pass *render_pass,
                                const struct ky_render_texture_options *options);

void ky_render_pass_add_rect(struct wlr_render_pass *render_pass,
                             const struct ky_render_rect_options *options);

#endif /* _RENDER_PASS_H_ */
