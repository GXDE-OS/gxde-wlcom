#ifndef _CONFIG_P_H_
#define _CONFIG_P_H_

#include "config.h"

struct config_manager {
    struct wl_list configs;

    char *file;
    json_object *json;

    /* dbus support */
    struct wl_event_source *event;
    sd_bus *bus;

    struct wl_listener display_destroy;
    struct wl_listener server_ready;
    struct wl_listener server_destroy;
};

bool config_manager_common_init(struct config_manager *config_manager);

void shortcut_init(void);

#endif /* _CONFIG_P_H_ */
