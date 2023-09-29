// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _XWAYLAND_H_
#define _XWAYLAND_H_

#include <stdbool.h>

struct server;
struct wl_client;

#if HAVE_XWAYLAND

bool xwayland_server_create(struct server *server);

void xwayland_server_destroy(void);

bool xwayland_check_client(struct wl_client *client);

float xwayland_get_scale(void);

void xwayland_set_scale(float scale);

float xwayland_unscale(int value);

float xwayland_scale(int value);

#else

static __attribute__((unused)) inline bool xwayland_server_create(struct server *server)
{
    return false;
}

static __attribute__((unused)) inline void xwayland_server_destroy(void) {}

static __attribute__((unused)) inline bool xwayland_client(struct wl_client *client)
{
    return false;
}

static __attribute__((unused)) float xwayland_get_scale(void)
{
    return 1.0;
}

static __attribute__((unused)) void xwayland_set_scale(float scale) {}

static __attribute__((unused)) float xwayland_unscale(int value)
{
    return value;
}

static __attribute__((unused)) float xwayland_scale(int value)
{
    return value;
}

#endif

#endif /* _XWAYLAND_H_ */
