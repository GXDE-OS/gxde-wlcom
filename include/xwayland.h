// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _XWAYLAND_H_
#define _XWAYLAND_H_

#include <stdbool.h>

struct seat;
struct server;
struct wl_client;
struct wl_global;

#if HAVE_XWAYLAND

bool xwayland_server_create(struct server *server);

void xwayland_server_destroy(void);

bool xwayland_check_client(const struct wl_client *client);

int xwayland_unscale(int value);

float xwayland_scale(int value);

void xwayland_set_cursor(struct seat *seat);

#else

// clang-format off

#define INLINE static __attribute__((unused)) inline

INLINE bool xwayland_server_create(struct server *server) { return false; }

INLINE void xwayland_server_destroy(void) {}

INLINE bool xwayland_check_client(const struct wl_client *client) { return false; }

INLINE int xwayland_unscale(int value) { return value; }

INLINE float xwayland_scale(int value) { return value; }

INLINE void xwayland_set_cursor(struct seat *seat) {}

// clang-format on

#undef INLINE

#endif

#endif /* _XWAYLAND_H_ */
