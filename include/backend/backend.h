// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _BACKEND_H_
#define _BACKEND_H_

#include <wayland-server-core.h>

struct wlr_session;
struct wlr_backend;

struct wlr_backend *ky_backend_autocreate(struct wl_display *display,
                                          struct wlr_session **session_ptr);

#endif /* _BACKEND_H_ */
