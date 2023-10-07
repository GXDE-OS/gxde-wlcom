// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _XWAYLAND_H_
#define _XWAYLAND_H_

#include <stdbool.h>

struct server;
struct wl_client;
struct wl_global;

#if HAVE_XWAYLAND

bool xwayland_server_create(struct server *server);

void xwayland_server_destroy(void);

bool xwayland_check_client(const struct wl_client *client);

bool xwayland_filter_global(const struct wl_client *client, const struct wl_global *global);

float xwayland_unscale(int value);

float xwayland_scale(int value);

#else

// clang-format off

#define INLINE static __attribute__((unused)) inline

INLINE bool xwayland_server_create(struct server *server) { return false; }

INLINE void xwayland_server_destroy(void) {}

INLINE bool xwayland_check_client(const struct wl_client *client) { return false; }

INLINE bool xwayland_filter_global(const struct wl_client *client, const struct wl_global *global) { return true; }

INLINE float xwayland_unscale(int value) { return value; }

INLINE float xwayland_scale(int value) { return value; }

// clang-format on

#undef INLINE

#endif

#endif /* _XWAYLAND_H_ */
