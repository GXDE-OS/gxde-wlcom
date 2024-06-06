// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _RENDERER_P_H_
#define _RENDERER_P_H_

#include <wlr/render/wlr_renderer.h>

bool wayland_drm_create(struct wl_display *display, struct wlr_renderer *renderer, int master_fd);

struct wlr_buffer *shm_create_buffer(int width, int height, uint32_t format);

#endif /* _RENDERER_P_H_ */
