// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdio.h>

#include <kywc/log.h>
#include <sys/stat.h>

#include "config_p.h"
#include "input/input.h"
#include "server.h"
#include "util/file.h"
#include "util/string.h"

static struct config_manager *config_manager = NULL;

static const char *check_config_file(void)
{
    char *config_dir = string_expand_path("~/.config/kylin-wlcom");
    if (!config_dir) {
        return NULL;
    }

    /* now check config dir */
    if (!file_exists(config_dir)) {
        kywc_log(KYWC_INFO, "configure dir %s not exist, create it", config_dir);
        int ret = mkdir(config_dir, S_IRWXU | S_IRWXG);
        if (ret) {
            kywc_log_errno(KYWC_ERROR, "create configure dir failed");
            free(config_dir);
            return NULL;
        }
    }

    const char *fullpath = string_join_path(config_dir, NULL, "config.json");
    free(config_dir);

    return fullpath;
}

// TODO: add config arg to check synced flag
void config_manager_sync(void)
{
    if (!config_manager->file || !config_manager->json) {
        return;
    }

    json_object_to_file_ext(config_manager->file, config_manager->json,
                            JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&config_manager->server_destroy.link);

    struct config *config, *config_tmp;
    wl_list_for_each_safe(config, config_tmp, &config_manager->configs, link) {
        config_destroy(config);
    }

    config_manager_sync();
    json_object_put(config_manager->json);
    json_object_put(config_manager->sys_json);
    free((void *)config_manager->file);

    free(config_manager);
    config_manager = NULL;
}

static void handle_server_ready(struct wl_listener *listener, void *data)
{
    input_action_manager_create(config_manager->server);
    kde_global_accel_manager_create(config_manager);
    kde_input_manager_create(config_manager);
    ukui_gsettings_create(config_manager);
    ukui_shortcut_manager_create(config_manager);
    ukui_view_mode_manager_create(config_manager);
}

struct config_manager *config_manager_create(struct server *server)
{
    config_manager = calloc(1, sizeof(struct config_manager));
    if (!config_manager) {
        return NULL;
    }

    /* read config file */
    config_manager->file = check_config_file();
    if (config_manager->file) {
        config_manager->json = json_object_from_file(config_manager->file);
    }
    /* get system default config */
    config_manager->sys_json = json_object_from_file("/etc/kylin-wlcom/config.json");
    kywc_log(KYWC_INFO, "get the sys default config from the etc directory");
    if (!config_manager->sys_json) {
        kywc_log(KYWC_WARN, "the default config does not exist");
    }

    /* create one if empty */
    if (!config_manager->json) {
        config_manager->json = json_object_new_object();
    }

    config_manager->server = server;
    wl_list_init(&config_manager->configs);

    config_manager->server_ready.notify = handle_server_ready;
    wl_signal_add(&server->events.ready, &config_manager->server_ready);
    config_manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &config_manager->server_destroy);

    config_manager_common_init(config_manager);

    return config_manager;
}

struct config *config_manager_add_config(const char *name)
{
    struct config *config = calloc(1, sizeof(struct config));
    if (!config) {
        return NULL;
    }

    json_object *object = config_manager->json;
    json_object *sys_object = config_manager->sys_json;
    if (name) {
        object = json_object_object_get(config_manager->json, name);
        if (!object) {
            object = json_object_new_object();
            json_object_object_add(config_manager->json, name, object);
        }
        sys_object = json_object_object_get(config_manager->sys_json, name);
    }
    config->json = object;
    config->sys_json = sys_object;

    wl_signal_init(&config->events.destroy);
    wl_list_insert(&config_manager->configs, &config->link);
    return config;
}

void config_destroy(struct config *config)
{
    wl_signal_emit_mutable(&config->events.destroy, NULL);
    wl_list_remove(&config->link);
    free(config);
}
