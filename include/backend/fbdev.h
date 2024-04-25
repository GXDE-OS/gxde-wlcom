// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _BACKEND_FBDEV_H_
#define _BACKEND_FBDEV_H_

#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>

struct wlr_backend *fbdev_backend_create(struct wl_display *display, struct wlr_session *session,
                                         const char *device);

bool wlr_backend_is_fbdev(struct wlr_backend *backend);

bool wlr_output_is_fbdev(struct wlr_output *output);

#endif /* _BACKEND_FBDEV_H_ */
