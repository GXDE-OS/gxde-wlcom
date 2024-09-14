// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include "render/pass.h"
#include "render/opengl.h"

bool ky_render_pass_submit(struct wlr_render_pass *render_pass, uint32_t quirks)
{
    if (wlr_render_pass_is_opengl(render_pass)) {
        struct ky_opengl_render_pass *opengl_pass =
            ky_opengl_render_pass_from_wlr_render_pass(render_pass);
        if (opengl_pass->impl && opengl_pass->impl->submit) {
            return opengl_pass->impl->submit(render_pass, quirks);
        }
    }

    return wlr_render_pass_submit(render_pass);
}

void ky_render_pass_add_texture(struct wlr_render_pass *render_pass,
                                const struct ky_render_texture_options *options)
{
    if (wlr_render_pass_is_opengl(render_pass)) {
        struct ky_opengl_render_pass *opengl_pass =
            ky_opengl_render_pass_from_wlr_render_pass(render_pass);
        if (opengl_pass->impl && opengl_pass->impl->add_texture) {
            opengl_pass->impl->add_texture(render_pass, options);
            return;
        }
    }

    wlr_render_pass_add_texture(render_pass, &options->base);
}

void ky_render_pass_add_rect(struct wlr_render_pass *render_pass,
                             const struct ky_render_rect_options *options)
{
    if (wlr_render_pass_is_opengl(render_pass)) {
        struct ky_opengl_render_pass *opengl_pass =
            ky_opengl_render_pass_from_wlr_render_pass(render_pass);
        if (opengl_pass->impl && opengl_pass->impl->add_rect) {
            opengl_pass->impl->add_rect(render_pass, options);
            return;
        }
    }

    wlr_render_pass_add_rect(render_pass, &options->base);
}
