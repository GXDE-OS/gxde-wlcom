// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _UTIL_WAYLAND_H_
#define _UTIL_WAYLAND_H_

#include <wayland-server-core.h>

/**
 * Destroy a transient global.
 *
 * Globals that are created and destroyed on the fly need special handling to
 * prevent race conditions with wl_registry. Use this function to destroy them.
 */
void wl_global_destroy_safe(struct wl_global *global);

/**
 * assume there is at most one listener in signal.
 */
void wl_signal_emit_oneshot(struct wl_signal *signal, void *data);

#endif /* _UTIL_WAYLAND_H_ */
