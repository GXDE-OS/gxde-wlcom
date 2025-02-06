// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <gio/gio.h>

#include "config_p.h"
#include "effect/effect.h"
#include "input/input.h"
#include "server.h"
#include "theme.h"
#include "util/dbus.h"

struct ukui_settings {
    struct {
        GSettings *settings;
        char *theme;
        int size;
    } cursor;

    struct {
        GSettings *settings;
        char *font_name;
        char *font_size;
        enum theme_type type;
        char *widget_theme;
    } style;

    struct wl_listener destroy;
};

#define UKUI_THEME_LIGHT "ukui-light"
#define UKUI_THEME_DARK "ukui-dark"

static const char *cursor_path = "/org/ukui/desktop/peripherals/mouse/";
#define CURSOR_PATH_LEN (36)
static const char *style_path = "/org/ukui/style/";
#define STYLE_PATH_LEN (16)

static const char *cursor_schema = "org.ukui.peripherals-mouse";
static const char *cursor_theme_key = "cursor-theme";
static const char *cursor_size_key = "cursor-size";
static const char *locate_pointer_key = "locate-pointer";
static const char *shake_cursor_key = "shake-cursor";

static const char *style_schema = "org.ukui.style";
static const char *style_name_key = "style-name";
static const char *icon_theme_key = "icon-theme-name";
static const char *widget_theme_key = "widget-theme-name";
static const char *font_name_key = "system-font";
static const char *font_size_key = "system-font-size";
static const char *accent_color_key = "theme-color";
static const char *window_radius_key = "window-radius";
static const char *menu_transparency_key = "menu-transparency";

static struct ukui_accent_color {
    char *name;
    int32_t color;
} ukui_accent_colors[] = {
    { "daybreakBlue", 0x3790FA }, { "jamPurple", 0x722ED1 },    { "magenta", 0xEB3096 },
    { "sunRed", 0xF3222D },       { "sunsetOrange", 0xF68C27 }, { "dustGold", 0xFFD966 },
    { "polarGreen", 0x52C429 },
};

static struct ukui_settings *settings = NULL;

static GSettingsSchema *is_schema_installed(const char *schema_id)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    return g_settings_schema_source_lookup(source, schema_id, TRUE);
}

static void handle_cursor_settings_changed(GSettings *mouse, const char *key)
{
    if (strcmp(key, locate_pointer_key) == 0) {
        bool enabled = g_settings_get_boolean(mouse, key);
        struct effect *effect = effect_by_name("locate_pointer");
        if (effect) {
            effect_set_enabled(effect, enabled);
        }
        return;
    } else if (strcmp(key, shake_cursor_key) == 0) {
        bool enabled = g_settings_get_boolean(mouse, key);
        struct effect *effect = effect_by_name("shake_cursor");
        if (effect) {
            effect_set_enabled(effect, enabled);
        }
        return;
    }

    if (strcmp(key, cursor_theme_key) == 0) {
        free(settings->cursor.theme);
        settings->cursor.theme = g_settings_get_string(mouse, key);
    } else if (strcmp(key, cursor_size_key) == 0) {
        settings->cursor.size = g_settings_get_int(mouse, key);
    } else {
        return;
    }

    if (settings->cursor.theme && settings->cursor.size > 0) {
        input_set_all_cursor(settings->cursor.theme, settings->cursor.size);
    }
}

static void style_name_changed(GSettings *style, const char *key)
{
    if (strcmp(widget_theme_key, key) == 0) {
        free(settings->style.widget_theme);
        settings->style.widget_theme = g_settings_get_string(style, key);
    } else {
        const char *style_name = g_settings_get_string(style, key);
        if (!strcmp(style_name, UKUI_THEME_LIGHT)) {
            settings->style.type = THEME_TYPE_LIGHT;
        } else if (!strcmp(style_name, UKUI_THEME_DARK)) {
            settings->style.type = THEME_TYPE_DARK;
        }
        free((void *)style_name);
    }

    if (settings->style.type != THEME_TYPE_UNDEFINED) {
        theme_manager_set_widget_theme(settings->style.widget_theme, settings->style.type);
    }
}

static void icon_theme_changed(GSettings *style, const char *key)
{
    const char *icon_theme = g_settings_get_string(style, key);
    theme_manager_set_icon_theme(icon_theme);
    free((void *)icon_theme);
}

static void font_style_changed(GSettings *style, const char *key)
{
    if (strcmp(key, font_name_key) == 0) {
        free(settings->style.font_name);
        settings->style.font_name = g_settings_get_string(style, key);
    } else {
        free(settings->style.font_size);
        settings->style.font_size = g_settings_get_string(style, key);
    }
    if (settings->style.font_name && settings->style.font_size) {
        theme_manager_set_font(settings->style.font_name, atoi(settings->style.font_size));
    }
}

static void accent_color_changed(GSettings *style, const char *key)
{
    const char *accent_color = g_settings_get_string(style, key);
    for (size_t i = 0; i < sizeof(ukui_accent_colors) / sizeof(struct ukui_accent_color); i++) {
        if (strcmp(accent_color, ukui_accent_colors[i].name) == 0) {
            theme_manager_set_accent_color(ukui_accent_colors[i].color);
            break;
        }
    }
    free((void *)accent_color);
}

static void window_radius_changed(GSettings *style, const char *key)
{
    int window_radius = g_settings_get_int(style, key);
    theme_manager_set_corner_radius(window_radius);
}

static void menu_transparency_changed(GSettings *style, const char *key)
{
    int menu_transparency = g_settings_get_int(style, key);
    theme_manager_set_opacity(menu_transparency);
}

static void handle_style_settings_changed(GSettings *style, const char *key)
{
    if (strcmp(key, style_name_key) == 0 || (strcmp(key, widget_theme_key) == 0)) {
        style_name_changed(style, key);
    } else if (strcmp(key, icon_theme_key) == 0) {
        icon_theme_changed(style, key);
    } else if (strcmp(key, font_name_key) == 0 || strcmp(key, font_size_key) == 0) {
        font_style_changed(style, key);
    } else if (strcmp(key, accent_color_key) == 0) {
        accent_color_changed(style, key);
    } else if (strcmp(key, window_radius_key) == 0) {
        window_radius_changed(style, key);
    } else if (strcmp(key, menu_transparency_key) == 0) {
        menu_transparency_changed(style, key);
    }
}

static int dconf_notify(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *prefix;
    CK(sd_bus_message_read_basic(msg, 's', &prefix));
    CK(sd_bus_message_enter_container(msg, 'a', "s"));

    const char *change;
    char key[128];
    int ret = 0;

    /* get current changed key */
    while (true) {
        ret = sd_bus_message_read(msg, "s", &change);
        if (ret < 0) {
            return ret;
        } else if (ret == 0) {
            break;
        }
        snprintf(key, 128, "%s%s", prefix, change);

        if (strncmp(key, style_path, STYLE_PATH_LEN) == 0) {
            handle_style_settings_changed(settings->style.settings, key + STYLE_PATH_LEN);
        } else if (strncmp(key, cursor_path, CURSOR_PATH_LEN) == 0) {
            handle_cursor_settings_changed(settings->cursor.settings, key + CURSOR_PATH_LEN);
        }
    }

    return 0;
}

static GSettings *init_schema_settings(const char *schema_id,
                                       void (*handler)(GSettings *settings, const char *key))
{
    GSettingsSchema *schema = is_schema_installed(schema_id);
    if (!schema) {
        return NULL;
    }

    GSettings *settings = g_settings_new(schema_id);
    if (!settings) {
        g_settings_schema_unref(schema);
        return NULL;
    }

    gchar **keys = g_settings_schema_list_keys(schema);
    for (int i = 0; keys[i] != NULL; i++) {
        handler(settings, keys[i]);
    }

    g_strfreev(keys);
    g_settings_schema_unref(schema);

    return settings;
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&settings->destroy.link);

    g_object_unref(settings->cursor.settings);
    g_object_unref(settings->style.settings);

    free(settings->cursor.theme);
    free(settings->style.font_name);
    free(settings->style.font_size);
    free(settings->style.widget_theme);

    free(settings);
}

bool ukui_gsettings_create(struct config_manager *config_manager)
{
    settings = calloc(1, sizeof(struct ukui_settings));
    if (!settings) {
        return false;
    }

    settings->style.type = THEME_TYPE_UNDEFINED;
    settings->cursor.settings = init_schema_settings(cursor_schema, handle_cursor_settings_changed);
    settings->style.settings = init_schema_settings(style_schema, handle_style_settings_changed);

    if (!settings->cursor.settings && !settings->style.settings) {
        free(settings);
        settings = NULL;
        return false;
    }

    /* monitor dconf dbus notify */
    dbus_match_signal(NULL, "/ca/desrt/dconf/Writer/user", "ca.desrt.dconf.Writer", "Notify",
                      dconf_notify, NULL);

    settings->destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(config_manager->server->display, &settings->destroy);

    return true;
}
