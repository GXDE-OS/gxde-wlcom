// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _RENDERER_P_H_
#define _RENDERER_P_H_

#include <wlr/render/wlr_renderer.h>

bool wayland_drm_create(struct wl_display *display, struct wlr_renderer *renderer, int master_fd);

#endif /* _RENDERER_P_H_ */
