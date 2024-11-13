// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _XWAYLAND_H_
#define _XWAYLAND_H_

#include <stdbool.h>

struct seat;
struct server;
struct wl_client;
struct wl_global;
struct view;

#if HAVE_XWAYLAND

bool xwayland_server_create(struct server *server);

void xwayland_server_destroy(void);

bool xwayland_check_client(const struct wl_client *client);

bool xwayland_check_view(struct view *view);

int xwayland_unscale(int value);

float xwayland_scale(int value);

float xwayland_get_scale(void);

void xwayland_set_cursor(struct seat *seat);

void xwayland_update_workarea(void);

#else

// clang-format off

#define INLINE static __attribute__((unused)) inline

INLINE bool xwayland_server_create(struct server *server) { return false; }

INLINE void xwayland_server_destroy(void) {}

INLINE bool xwayland_check_client(const struct wl_client *client) { return false; }

INLINE bool xwayland_check_view(struct view *view) { return false; }

INLINE int xwayland_unscale(int value) { return value; }

INLINE float xwayland_scale(int value) { return value; }

INLINE float xwayland_get_scale(void) { return 1.0; }

INLINE void xwayland_set_cursor(struct seat *seat) {}

INLINE void xwayland_update_workarea(void) {}

// clang-format on

#undef INLINE

#endif

#endif /* _XWAYLAND_H_ */
