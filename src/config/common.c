// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <malloc.h>

#include "config_p.h"
#include "server.h"
#include "util/logger.h"

static const char *service_path = "/com/kylin/Wlcom";
static const char *service_interface = "com.kylin.Wlcom";

static struct {
    struct wl_listener destroy;
    struct wl_event_source *timer;
} config = { 0 };

static int set_log_level(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    uint32_t level = 0;
    CK(sd_bus_message_read(m, "u", &level));

    if (level < KYWC_LOG_LEVEL_LAST) {
        logger_set_level(level);
        return sd_bus_reply_method_return(m, NULL);
    }

    const sd_bus_error error =
        SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild args, please input [0-5].");
    return sd_bus_reply_method_error(m, &error);
}

static int print_config(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct config_manager *cm = userdata;
    sd_bus_message *reply = NULL;

    CK(sd_bus_message_new_method_return(m, &reply));
    const char *config = json_object_to_json_string(cm->json);
    sd_bus_message_append_basic(reply, 's', config);
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static int handle_timeout(void *data)
{
    wl_event_source_timer_update(config.timer, 5000);
    malloc_trim(4096);
    return 0;
}

static int trim_memory(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    uint32_t enabled = 0;
    CK(sd_bus_message_read(m, "b", &enabled));

    if (!!enabled == !!config.timer) {
        return sd_bus_reply_method_return(m, NULL);
    }

    struct config_manager *cm = userdata;
    if (enabled) {
        config.timer = wl_event_loop_add_timer(cm->server->event_loop, handle_timeout, cm);
        wl_event_source_timer_update(config.timer, 5000);
    } else {
        wl_event_source_remove(config.timer);
        config.timer = NULL;
    }

    return sd_bus_reply_method_return(m, NULL);
}

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("SetLogLevel", "u", "", set_log_level, 0),
    SD_BUS_METHOD("PrintConfig", "", "s", print_config, 0),
    SD_BUS_METHOD("TrimMemory", "b", "", trim_memory, 0),
    SD_BUS_VTABLE_END,
};

static void handle_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&config.destroy.link);
    if (config.timer) {
        wl_event_source_remove(config.timer);
    }
}

bool config_manager_common_init(struct config_manager *config_manager)
{
    if (!config_manager_add_config(NULL, NULL, service_path, service_interface, service_vtable,
                                   config_manager)) {
        return false;
    }

    config.destroy.notify = handle_destroy;
    wl_display_add_destroy_listener(config_manager->server->display, &config.destroy);

    return true;
}
