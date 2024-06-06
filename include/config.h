// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <json-c/json.h>
#include <systemd/sd-bus.h>
#include <wayland-server-core.h>

#define CK(v)                                                                                      \
    do {                                                                                           \
        int tmp = (v);                                                                             \
        if (tmp < 0)                                                                               \
            return tmp;                                                                            \
    } while (0)

struct server;

struct config {
    struct wl_list link;
    json_object *json;
    sd_bus_slot *slot;

    struct {
        struct wl_signal destroy;
    } events;
};

struct config_manager *config_manager_create(struct server *server);

struct config *config_manager_add_config(const char *name, const char *bus, const char *path,
                                         const char *interface, const sd_bus_vtable *vtable,
                                         void *data);

void config_destroy(struct config *config);

void config_notify(const char *title, const char *body, const char *icon);

void config_manager_sync(void);

#endif /* _CONFIG_H_ */
