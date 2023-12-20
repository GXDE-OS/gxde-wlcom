// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _RENDERER_H_
#define _RENDERER_H_

#include <stdbool.h>
#include <wayland-server-core.h>

struct wlr_backend;

struct wlr_renderer *ky_renderer_autocreate(struct wlr_backend *backend);

bool ky_renderer_init_wl_display(struct wlr_renderer *renderer, struct wlr_backend *backend,
                                 struct wl_display *wl_display);

bool ky_wayland_buffer_create(struct wl_display *wl_display, struct wlr_renderer *wlr_renderer);

const struct wlr_drm_format *ky_renderer_get_render_format(struct wlr_renderer *renderer,
                                                           uint32_t fmt);

#endif /* _RENDER_H_ */
