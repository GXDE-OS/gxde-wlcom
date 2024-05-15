// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "theme_p.h"

static const char *service_path = "/com/kylin/Wlcom/Theme";
static const char *service_interface = "com.kylin.Wlcom.Theme";

static int list_themes(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    struct theme_manager *manager = userdata;

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(msg, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "s"));

    struct theme *theme;
    wl_list_for_each(theme, &manager->themes, link) {
        sd_bus_message_append(reply, "s", theme_name_from_theme_type(theme->theme_type));
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static int current_theme(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    struct theme_manager *manager = userdata;
    return sd_bus_reply_method_return(msg, "s",
                                      theme_name_from_theme_type(manager->current->theme_type));
}

static int set_theme(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    char *theme = NULL;
    CK(sd_bus_message_read(msg, "s", &theme));
    enum theme_type theme_type = THEME_TYPE_DEFAULT;
    if (!strcmp(theme, WLCOM_THEME_LIGHT)) {
        theme_type = THEME_TYPE_LIGHT;
    } else if (!strcmp(theme, WLCOM_THEME_DARK)) {
        theme_type = THEME_TYPE_DARK;
    }
    int32_t ret = theme_manager_set_theme(theme_type);
    return sd_bus_reply_method_return(msg, "b", &ret);
}

static int current_font(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    struct theme_manager *manager = userdata;
    return sd_bus_reply_method_return(msg, "si", manager->current->font_name,
                                      manager->current->font_size);
}

static int set_font(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    char *font_name = NULL;
    int32_t size;
    CK(sd_bus_message_read(msg, "si", &font_name, &size));
    int32_t ret = theme_manager_set_font(font_name, size);
    return sd_bus_reply_method_return(msg, "b", &ret);
}

static int set_accent_color(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    int32_t accent_color;
    CK(sd_bus_message_read(msg, "i", &accent_color));
    int32_t ret = theme_manager_set_accent_color(accent_color);
    return sd_bus_reply_method_return(msg, "b", &ret);
}

static int set_icon_theme(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    char *icon_theme_name = NULL;
    CK(sd_bus_message_read(msg, "s", &icon_theme_name));
    int32_t ret = theme_manager_set_icon_theme(icon_theme_name);
    return sd_bus_reply_method_return(msg, "b", &ret);
}

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListAllThemes", "", "as", list_themes, 0),
    SD_BUS_METHOD("currentTheme", "", "s", current_theme, 0),
    SD_BUS_METHOD("SetTheme", "s", "b", set_theme, 0),
    SD_BUS_METHOD("currentFont", "", "si", current_font, 0),
    SD_BUS_METHOD("SetFont", "si", "b", set_font, 0),
    SD_BUS_METHOD("SetAccentColor", "i", "b", set_accent_color, 0),
    SD_BUS_METHOD("SetIconTheme", "s", "b", set_icon_theme, 0),
    SD_BUS_VTABLE_END,
};

bool theme_manager_config_init(struct theme_manager *manager)
{
    manager->config = config_manager_add_config("theme", NULL, service_path, service_interface,
                                                service_vtable, manager);
    return !!manager->config;
}

const char *theme_manager_read_config(struct theme_manager *manager)
{
    if (!manager->config || !manager->config->json) {
        return NULL;
    }

    json_object *data;
    /* some override configs */
    if (json_object_object_get_ex(manager->config->json, "font_name", &data)) {
        free(manager->override.font_name);
        manager->override.font_name = strdup(json_object_get_string(data));
    }
    if (json_object_object_get_ex(manager->config->json, "font_size", &data)) {
        manager->override.font_size = json_object_get_int(data);
    }
    if (json_object_object_get_ex(manager->config->json, "accent_color", &data)) {
        manager->override.accent_color = json_object_get_int(data);
    } else {
        manager->override.accent_color = -1;
    }

    if (json_object_object_get_ex(manager->config->json, "corner_radius", &data)) {
        manager->override.corner_radius = json_object_get_int(data);
    } else {
        manager->override.corner_radius = -1;
    }

    if (json_object_object_get_ex(manager->config->json, "opacity", &data)) {
        manager->override.opacity = json_object_get_int(data);
    } else {
        manager->override.opacity = -1;
    }

    if (json_object_object_get_ex(manager->config->json, "name", &data)) {
        return json_object_get_string(data);
    }

    return NULL;
}

void theme_manager_write_config(struct theme_manager *manager, const char *name)
{
    if (name && name[0]) {
        json_object_object_add(manager->config->json, "name", json_object_new_string(name));
    }

    if (manager->override.font_name) {
        json_object_object_add(manager->config->json, "font_name",
                               json_object_new_string(manager->override.font_name));
    }

    if (manager->override.font_size > 0) {
        json_object_object_add(manager->config->json, "font_size",
                               json_object_new_int(manager->override.font_size));
    }

    if (manager->override.accent_color >= 0) {
        json_object_object_add(manager->config->json, "accent_color",
                               json_object_new_int(manager->override.accent_color));
    }

    if (manager->override.corner_radius >= 0) {
        json_object_object_add(manager->config->json, "corner_radius",
                               json_object_new_int(manager->override.corner_radius));
    }

    if (manager->override.opacity >= 0) {
        json_object_object_add(manager->config->json, "opacity",
                               json_object_new_int(manager->override.opacity));
    }
}

const char *theme_manager_read_icon_config(struct theme_manager *manager)
{
    if (!manager->config || !manager->config->json) {
        return NULL;
    }

    json_object *data;
    if (json_object_object_get_ex(manager->config->json, "icon_theme_name", &data)) {
        return json_object_get_string(data);
    }

    return NULL;
}

void theme_manager_write_icon_config(struct theme_manager *manager, const char *name)
{
    if (name && name[0]) {
        if (strcmp(name, DEFAULT_ICON_THEME_NAME)) {
            json_object_object_add(manager->config->json, "icon_theme_name",
                                   json_object_new_string(name));
        } else {
            json_object_object_del(manager->config->json, "icon_theme_name");
        }
    }
}
