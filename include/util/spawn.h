// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _UTIL_SPAWN_H_
#define _UTIL_SPAWN_H_

#include <stdbool.h>
#include <sys/types.h>

#include <wayland-server-core.h>

bool spawn_invoke(const char *command);

pid_t spawn_session(const char *session);

void spawn_wait(pid_t pid);

struct wl_client *spawn_client(struct wl_display *display, const char *command);

#endif /* _SPAWN_H_ */
