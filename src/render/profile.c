// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include "render/profile.h"
#include "render/opengl.h"

#ifdef TRACY_ENABLE

void ky_profile_render_create(struct wlr_renderer *renderer)
{
    if (wlr_renderer_is_opengl(renderer)) {
        ky_profile_gl_create(renderer);
    }
}

void ky_profile_render_destroy(struct wlr_renderer *renderer)
{
    if (wlr_renderer_is_opengl(renderer)) {
        ky_profile_gl_destroy();
    }
}

void ky_profile_render_begin(struct wlr_renderer *renderer,
                             const struct ___tracy_source_location_data *data)
{
    if (wlr_renderer_is_opengl(renderer)) {
        ky_profile_gl_begin(data);
    }
}

void ky_profile_render_end(struct wlr_renderer *renderer)
{
    if (wlr_renderer_is_opengl(renderer)) {
        ky_profile_gl_end();
    }
}

void ky_profile_render_collect(struct wlr_renderer *renderer)
{
    if (wlr_renderer_is_opengl(renderer)) {
        ky_profile_gl_collect();
    }
}

#endif /* TRACY_ENABLE */
