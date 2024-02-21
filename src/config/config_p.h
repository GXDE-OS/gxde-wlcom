// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _CONFIG_P_H_
#define _CONFIG_P_H_

#include "config.h"

struct config_manager {
    struct server *server;
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

#if HAVE_KDE_GLOBAL_ACCEL
bool kde_global_accel_manager_create(struct config_manager *config_manager);
#else
static __attribute__((unused)) inline bool
kde_global_accel_manager_create(struct config_manager *config_manager)
{
    return false;
}
#endif

#if HAVE_KDE_INPUT
bool kde_input_manager_create(struct config_manager *config_manager);
#else
static __attribute__((unused)) inline bool
kde_input_manager_create(struct config_manager *config_manager)
{
    return false;
}
#endif

#if HAVE_KDE_GLOBAL_SETTINGS
bool kde_global_settings_create(struct config_manager *config_manager);
#else
static __attribute__((unused)) inline bool
kde_global_settings_create(struct config_manager *config_manager)
{
    return false;
}
#endif

#if HAVE_KDE_NIGHTCOLOR
bool kde_nightcolor_manager_create(struct config_manager *config_manager);
#else
static __attribute__((unused)) inline bool
kde_nightcolor_manager_create(struct config_manager *config_manager)
{
    return false;
}
#endif

#if HAVE_UKUI_SCREENSHOT
bool ukui_screenshot_create(struct config_manager *config_manager);
#else
static __attribute__((unused)) inline bool
ukui_screenshot_create(struct config_manager *config_manager)
{
    return false;
}
#endif

#if HAVE_UKUI_GSETTINGS
bool ukui_gsettings_create(struct config_manager *config_mananger);
#else
static __attribute__((unused)) inline bool
ukui_gsettings_create(struct config_manager *config_mananger)
{
    return false;
}
#endif

#endif /* _CONFIG_P_H_ */
