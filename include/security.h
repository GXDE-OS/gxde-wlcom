// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _SECURITY_H_
#define _SECURITY_H_

#include <pixman.h>
#include <wayland-server-core.h>

struct server;
struct output;
struct ky_scene_node;

struct security_client {
    struct wl_list link;

    struct wl_client *client;
    struct wl_listener destroy;
    struct wl_listener new_resource;

    int fd;
    pid_t pid;
    uid_t uid;
    gid_t gid;
    char *path;
};

typedef bool (*security_global_filter_func_t)(const struct security_client *client, void *data);

bool security_add_global_filter(struct wl_global *global, security_global_filter_func_t filter,
                                void *data);

void security_remove_global_filter(struct wl_global *global);

bool security_manager_create(struct server *server);

#if 0

bool security_check_node(struct ky_scene_node *node);

bool security_check_output(struct output *output, pixman_region32_t *clip);

#else

// clang-format off

#define INLINE static __attribute__((unused)) inline

INLINE bool security_check_node(struct ky_scene_node *node) { return false; }

INLINE bool security_check_output(struct output *output, pixman_region32_t *clip) { return false; }

// clang-format on

#undef INLINE

#endif

#endif /* _SECURITY_H_ */
