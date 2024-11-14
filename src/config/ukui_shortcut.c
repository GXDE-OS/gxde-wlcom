// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L

#include <kywc/log.h>
#include <string.h>

#include "config_p.h"
#include "kywc/binding.h"
#include "server.h"

struct shortcut_service {
    char *name;
    uint32_t black_mask; /* blacklist mask */
    uint32_t white_mask; /* whitelist mask */
    bool block_all;

    struct wl_list link;
};

struct ukui_shortcut_manager {
    sd_bus *bus;

    struct wl_list services;

    struct wl_listener destroy;
};

const char *service_path = "/org/ukui/settingsDaemon/shortcut";
const char *service_interface = "org.ukui.settingsDaemon.shortcut";

static struct ukui_shortcut_manager *shortcut_manager = NULL;

static int block_shortcuts_message_handler(sd_bus_message *msg, void *userdata, sd_bus_error *error)
{
    struct shortcut_service *service = userdata;

    CK(sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "s"));

    const char *type_name;
    while (sd_bus_message_read(msg, "s", &type_name) > 0) {
        bool found;
        enum key_binding_type type = kywc_key_binding_type_by_name(type_name, &found);
        if (!found) {
            continue;
        }
        kywc_log(KYWC_INFO, "block keybinding type: %s", type_name);

        /* blocking all */
        if (type == KEY_BINDING_TYPE_NUM) {
            service->block_all = true;
            kywc_key_binding_block_all(true);
            break;
        }

        /* blocking types based on backlist */
        kywc_key_binding_block_type(type, true);
        service->black_mask |= 1 << type;
    }

    CK(sd_bus_message_exit_container(msg));

    return 0;
}

static int unblock_shortcuts_message_handler(sd_bus_message *msg, void *userdata,
                                             sd_bus_error *error)
{
    struct shortcut_service *service = userdata;

    CK(sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "s"));

    const char *type_name;
    while (sd_bus_message_read(msg, "s", &type_name) > 0) {
        bool found;
        enum key_binding_type type = kywc_key_binding_type_by_name(type_name, &found);
        if (!found) {
            continue;
        }

        service->white_mask |= 1 << type;
    }

    kywc_log(KYWC_INFO, "unblock whitelist mask: %x", service->white_mask);
    if (service->white_mask) {
        for (size_t i = KEY_BINDING_TYPE_CUSTOM_DEF; i < KEY_BINDING_TYPE_NUM; i++) {
            /* blocking type is not in the whitelist */
            if (!(service->white_mask & (1 << i))) {
                kywc_log(KYWC_DEBUG, "block by binding type index: %ld", i);
                kywc_key_binding_block_type(i, true);
                continue;
            }
        }
    }

    CK(sd_bus_message_exit_container(msg));

    return 0;
}

static void shortcut_service_destroy(struct shortcut_service *service)
{
    for (size_t i = KEY_BINDING_TYPE_CUSTOM_DEF; i < KEY_BINDING_TYPE_NUM; i++) {
        /* recovery of blocked types that are not on the whitelist */
        if (service->white_mask && !(service->white_mask & (1 << i))) {
            kywc_key_binding_block_type(i, false);
        }

        /* this type is not in the backlist */
        if (!(service->black_mask & (1 << i))) {
            continue;
        }

        /* recovery of blocked types based on backlist */
        kywc_key_binding_block_type(i, false);
    }

    if (service->block_all) {
        kywc_key_binding_block_all(false);
    }

    wl_list_remove(&service->link);
    free(service->name);
    free(service);
}

static void ukui_shortcut_service_destroy(const char *name)
{
    struct shortcut_service *service, *service_tmp;
    wl_list_for_each_safe(service, service_tmp, &shortcut_manager->services, link) {
        if (strcmp(service->name, name)) {
            continue;
        }
        shortcut_service_destroy(service);
    }
}

static void ukui_shortcut_service_create(const char *name)
{
    struct shortcut_service *service = calloc(1, sizeof(*service));
    if (!service) {
        return;
    }

    if (sd_bus_call_method_async(shortcut_manager->bus, NULL, name, service_path, service_interface,
                                 "blockShortcuts", block_shortcuts_message_handler, service,
                                 NULL) < 0) {
        kywc_log_errno(KYWC_ERROR, "dbus call service:%s bolckShortcuts method failed", name);
    }

    if (sd_bus_call_method_async(shortcut_manager->bus, NULL, name, service_path, service_interface,
                                 "unblockShortcuts", unblock_shortcuts_message_handler, service,
                                 NULL) < 0) {
        kywc_log_errno(KYWC_ERROR, "dbus call service:%s unblockShortcuts method failed", name);
    }

    service->name = strdup(name);
    wl_list_insert(&shortcut_manager->services, &service->link);
}

static int service_message_handler(sd_bus_message *msg, void *userdata, sd_bus_error *error)
{
    const char *name = NULL, *old_owner = NULL, *new_owner = NULL;
    CK(sd_bus_message_read(msg, "sss", &name, &old_owner, &new_owner));
    if (!name || (strncmp(name, "org.ukui.settingsDaemon", 23))) {
        return 0;
    }

    if (old_owner && !*old_owner) {
        ukui_shortcut_service_create(name);
        kywc_log(KYWC_INFO, "service: %s registered with owner %s", name, new_owner);
    } else if (new_owner && !*new_owner) {
        ukui_shortcut_service_destroy(name);
        kywc_log(KYWC_INFO, "service: %s unregistered from owner %s", name, old_owner);
    }

    return 0;
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&shortcut_manager->destroy.link);

    struct shortcut_service *service, *service_tmp;
    wl_list_for_each_safe(service, service_tmp, &shortcut_manager->services, link) {
        shortcut_service_destroy(service);
    }

    free(shortcut_manager);
}

bool ukui_shortcut_manager_create(struct config_manager *config_manager)
{
    const char *match = "type=signal,interface=org.freedesktop.DBus,member=NameOwnerChanged";
    if (sd_bus_add_match_async(config_manager->bus, NULL, match, service_message_handler, NULL,
                               NULL) < 0) {
        kywc_log(KYWC_ERROR, "ukui_shortcuts_manager listener dbus service failed");
        return false;
    }

    shortcut_manager = calloc(1, sizeof(struct ukui_shortcut_manager));
    if (!shortcut_manager) {
        return false;
    }

    shortcut_manager->bus = config_manager->bus;

    wl_list_init(&shortcut_manager->services);

    shortcut_manager->destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(config_manager->server->display, &shortcut_manager->destroy);

    return true;
}
