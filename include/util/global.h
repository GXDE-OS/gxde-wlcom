// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _UTIL_GLOBAL_H
#define _UTIL_GLOBAL_H

#include <wayland-server-core.h>

/**
 * Destroy a transient global.
 *
 * Globals that are created and destroyed on the fly need special handling to
 * prevent race conditions with wl_registry. Use this function to destroy them.
 */
void kywc_global_destroy_safe(struct wl_global *global);

#endif
