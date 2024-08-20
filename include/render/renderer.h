// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _RENDERER_H_
#define _RENDERER_H_

#include <wayland-server-core.h>

struct wlr_backend;
struct wlr_allocator;

struct wlr_renderer *ky_renderer_autocreate(struct wlr_backend *backend);

bool ky_renderer_init_wl_display(struct wlr_renderer *renderer, struct wlr_backend *backend,
                                 struct wl_display *wl_display);

bool ky_wayland_buffer_create(struct wl_display *wl_display, struct wlr_renderer *wlr_renderer);

const struct wlr_drm_format *ky_renderer_get_render_format(struct wlr_renderer *renderer,
                                                           uint32_t fmt, bool single_plane);

struct wlr_buffer *ky_renderer_create_buffer(struct wlr_renderer *renderer,
                                             struct wlr_allocator *alloc, int width, int height,
                                             uint32_t format, bool single_plane);

bool ky_renderer_is_software(struct wlr_renderer *renderer);

#endif /* _RENDER_H_ */
