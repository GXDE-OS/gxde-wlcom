// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "theme_p.h"
#include "util/dbus.h"

static const char *service_path = "/com/kylin/Wlcom/Theme";
static const char *service_interface = "com.kylin.Wlcom.Theme";

static int print_theme_config(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    struct theme_manager *manager = userdata;
    const char *config = json_object_to_json_string(manager->config->json);
    return sd_bus_reply_method_return(msg, "s", config);
}

static int set_widget_theme(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    char *theme = NULL;
    uint32_t type = THEME_TYPE_DEFAULT;
    CK(sd_bus_message_read(msg, "su", &theme, &type));
    bool ret = theme_manager_set_widget_theme(theme, type);
    return sd_bus_reply_method_return(msg, "b", ret);
}

static int set_gtk_theme(sd_bus_message* msg, void* userdata, sd_bus_error* ret_error) {
    const char* theme = NULL;
    CK(sd_bus_message_read(msg, "s", &theme));
    bool ret = config_set_gtk_theme(theme);
    return sd_bus_reply_method_return(msg, "b", ret);
}

static int set_gtk_decoration_buttons(sd_bus_message* msg, void* userdata,
        sd_bus_error* ret_error) {
    struct theme_manager* manager = userdata;
    int minimize, maximize, close;
    CK(sd_bus_message_read(msg, "bbb", &minimize, &maximize, &close));

    bool ret = config_set_gtk_decoration_layout(minimize, maximize, close);
    if (ret) {
        manager->global.gtk_decoration_minimize = minimize;
        manager->global.gtk_decoration_maximize = maximize;
        manager->global.gtk_decoration_close = close;
        theme_manager_write_config(manager, THEME_TYPE_UNDEFINED);
        config_manager_sync();
    }

    return sd_bus_reply_method_return(msg, "b", ret);
}

static int get_gtk_decoration_buttons(sd_bus_message* msg, void* userdata,
        sd_bus_error* ret_error) {
    struct theme_manager* manager = userdata;
    return sd_bus_reply_method_return(
        msg, "bbb", manager->global.gtk_decoration_minimize,
        manager->global.gtk_decoration_maximize,
        manager->global.gtk_decoration_close);
}

static int set_icon_theme(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    char *icon_theme_name = NULL;
    CK(sd_bus_message_read(msg, "s", &icon_theme_name));
    bool ret = theme_manager_set_icon_theme(icon_theme_name);
    return sd_bus_reply_method_return(msg, "b", ret);
}

static int set_font(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    char *font_name = NULL;
    int32_t size;
    CK(sd_bus_message_read(msg, "si", &font_name, &size));
    bool ret = theme_manager_set_font(font_name, size);
    return sd_bus_reply_method_return(msg, "b", ret);
}

static int set_accent_color(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    int32_t accent_color;
    CK(sd_bus_message_read(msg, "i", &accent_color));
    bool ret = theme_manager_set_accent_color(accent_color);
    return sd_bus_reply_method_return(msg, "b", ret);
}

static int set_corner_radius(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    int32_t corner_radius;
    CK(sd_bus_message_read(msg, "i", &corner_radius));
    bool ret = theme_manager_set_corner_radius(corner_radius);
    return sd_bus_reply_method_return(msg, "b", ret);
}

static int set_opacity(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    int32_t opacity;
    CK(sd_bus_message_read(msg, "i", &opacity));
    bool ret = theme_manager_set_opacity(opacity);
    return sd_bus_reply_method_return(msg, "b", ret);
}

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("PrintThemeConfig", "", "s", print_theme_config, 0),
    SD_BUS_METHOD("SetWidgetTheme", "su", "b", set_widget_theme, 0),
    SD_BUS_METHOD("SetGtkTheme", "s", "b", set_gtk_theme, 0),
    SD_BUS_METHOD("GetGtkDecorationButtons", "", "bbb", get_gtk_decoration_buttons, 0),
    SD_BUS_METHOD("SetGtkDecorationButtons", "bbb", "b", set_gtk_decoration_buttons, 0),
    SD_BUS_METHOD("SetIconTheme", "s", "b", set_icon_theme, 0),
    SD_BUS_METHOD("SetFont", "si", "b", set_font, 0),
    SD_BUS_METHOD("SetAccentColor", "i", "b", set_accent_color, 0),
    SD_BUS_METHOD("SetCornerRadius", "i", "b", set_corner_radius, 0),
    SD_BUS_METHOD("SetOpacity", "i", "b", set_opacity, 0),
    SD_BUS_VTABLE_END,
};

bool theme_manager_config_init(struct theme_manager *manager)
{
    manager->config = config_manager_add_config("theme");
    if (!manager->config) {
        return false;
    }
    return dbus_register_object(NULL, service_path, service_interface, service_vtable, manager);
}

enum theme_type theme_manager_read_config(struct theme_manager *manager)
{
    if (!manager->config || !manager->config->json) {
        return THEME_TYPE_DEFAULT;
    }

    json_object *data;
    /* some global configs */
    free(manager->global.font_name);
    if (json_object_object_get_ex(manager->config->json, "font_name", &data)) {
        manager->global.font_name = strdup(json_object_get_string(data));
    } else {
        /* Patch: 标题栏字体对齐Deepin */
        manager->global.font_name = strdup("SourceHanSansSC");
    }
    if (json_object_object_get_ex(manager->config->json, "font_size", &data)) {
        manager->global.font_size = json_object_get_int(data);
    } else {
        /* Patch: 标题栏字号对齐Deepin */
        manager->global.font_size = 14;
    }

    if (json_object_object_get_ex(manager->config->json, "accent_color", &data)) {
        manager->global.accent_color = json_object_get_int(data);
    } else {
        manager->global.accent_color = -1;
    }

    if (json_object_object_get_ex(manager->config->json, "corner_radius", &data)) {
        manager->global.corner_radius = json_object_get_int(data);
    } else {
        manager->global.corner_radius = 8;
    }

    if (json_object_object_get_ex(manager->config->json, "opacity", &data)) {
        manager->global.opacity = json_object_get_int(data);
    } else {
        manager->global.opacity = 100;
    }

    if (json_object_object_get_ex(manager->config->json, "type", &data)) {
        return json_object_get_int(data);
    }

    /* get system default config */
    if (manager->config->sys_json &&
        json_object_object_get_ex(manager->config->sys_json, "type", &data)) {
        return json_object_get_int(data);
    }

    return THEME_TYPE_DEFAULT;
}

void theme_manager_read_gtk_decoration_config(struct theme_manager* manager) {
    struct global_theme* global = &manager->global;
    global->gtk_decoration_minimize = true;
    global->gtk_decoration_maximize = true;
    global->gtk_decoration_close = true;

    if (!manager->config || !manager->config->json) {
        return;
    }

    json_object* data;
    if (json_object_object_get_ex(manager->config->json, "gtk_decoration_minimize", &data)) {
        global->gtk_decoration_minimize = json_object_get_boolean(data);
    }
    if (json_object_object_get_ex(manager->config->json, "gtk_decoration_maximize", &data)) {
        global->gtk_decoration_maximize = json_object_get_boolean(data);
    }
    if (json_object_object_get_ex(manager->config->json, "gtk_decoration_close", &data)) {
        global->gtk_decoration_close = json_object_get_boolean(data);
    }
}

void theme_manager_write_config(struct theme_manager *manager, enum theme_type theme_type)
{
    if (!manager->config || !manager->config->json) {
        return;
    }

    if (theme_type > THEME_TYPE_UNDEFINED) {
        json_object_object_add(manager->config->json, "type", json_object_new_int(theme_type));
    }

    if (!manager->global.gtk_decoration_minimize) {
        json_object_object_add(manager->config->json, "gtk_decoration_minimize",
            json_object_new_boolean(false));
    } else {
        json_object_object_del(manager->config->json, "gtk_decoration_minimize");
    }

    if (!manager->global.gtk_decoration_maximize) {
        json_object_object_add(manager->config->json, "gtk_decoration_maximize",
            json_object_new_boolean(false));
    } else {
        json_object_object_del(manager->config->json, "gtk_decoration_maximize");
    }

    if (!manager->global.gtk_decoration_close) {
        json_object_object_add(manager->config->json, "gtk_decoration_close",
            json_object_new_boolean(false));
    } else {
        json_object_object_del(manager->config->json, "gtk_decoration_close");
    }

    /* Patch: 主题默认字体对齐Deepin */
    if (manager->global.font_name && strcmp(manager->global.font_name, "SourceHanSansSC")) {
        json_object_object_add(manager->config->json, "font_name",
                               json_object_new_string(manager->global.font_name));
    } else {
        json_object_object_del(manager->config->json, "font_name");
    }

    /* Patch: 主题字号默认大小对齐Deepin */
    if (manager->global.font_size != 14) {
        json_object_object_add(manager->config->json, "font_size",
                               json_object_new_int(manager->global.font_size));
    } else {
        json_object_object_del(manager->config->json, "font_size");
    }

    if (manager->global.accent_color >= 0) {
        json_object_object_add(manager->config->json, "accent_color",
                               json_object_new_int(manager->global.accent_color));
    }

    if (manager->global.corner_radius != 8) {
        json_object_object_add(manager->config->json, "corner_radius",
                               json_object_new_int(manager->global.corner_radius));
    } else {
        json_object_object_del(manager->config->json, "corner_radius");
    }

    if (manager->global.opacity != 100) {
        json_object_object_add(manager->config->json, "opacity",
                               json_object_new_int(manager->global.opacity));
    } else {
        json_object_object_del(manager->config->json, "opacity");
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

    /* get system default config */
    if (manager->config->sys_json &&
        json_object_object_get_ex(manager->config->sys_json, "icon_theme_name", &data)) {
        return json_object_get_string(data);
    }

    return NULL;
}

void theme_manager_write_icon_config(struct theme_manager *manager, const char *name)
{
    if (!manager->config || !manager->config->json) {
        return;
    }

    if (strcmp(name, FALLBACK_ICON_THEME_NAME)) {
        json_object_object_add(manager->config->json, "icon_theme_name",
                               json_object_new_string(name));
    } else {
        json_object_object_del(manager->config->json, "icon_theme_name");
    }
}
