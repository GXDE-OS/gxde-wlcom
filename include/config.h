// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <json-c/json.h>
#include <wayland-server-core.h>

struct server;

struct config {
    struct wl_list link;
    json_object *json;
    json_object *sys_json;

    struct {
        struct wl_signal destroy;
    } events;
};

struct config_manager *config_manager_create(struct server *server);

struct config *config_manager_add_config(const char *name);

void config_destroy(struct config *config);

void config_manager_sync(void);

bool config_set_gtk_theme(const char *name);
bool config_set_icon_theme(const char *name);
bool config_set_gtk_decoration_layout(bool minimize, bool maximize, bool close);

#endif /* _CONFIG_H_ */
