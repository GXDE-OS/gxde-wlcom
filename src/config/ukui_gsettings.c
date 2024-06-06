// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <gio/gio.h>

#include "config_p.h"
#include "input/input.h"
#include "server.h"
#include "theme.h"

struct ukui_settings {
    struct {
        GSettings *settings;
        char *theme;
        int size;
    } cursor;

    struct {
        GSettings *settings;
        char *style_name;
        char *icon_theme;
        char *accent_color;
        char *font_name;
        char *font_size;
        int window_radius;
        int menu_transparency;
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

static const char *style_schema = "org.ukui.style";
static const char *style_name_key = "style-name";
static const char *icon_theme_key = "icon-theme-name";
static const char *font_name_key = "system-font";
static const char *font_size_key = "system-font-size";
static const char *accent_color_key = "theme-color";
static const char *window_radius_key = "window-radius";
static const char *menu_transparency_key = "menu-transparency";

static struct ukui_accent_color {
    char *name;
    float accent_color[4];
} ukui_accent_colors[] = {
    { "daybreakBlue", { 55.0 / 255, 144.0 / 255, 250.0 / 255, 1.0 } },
    { "jamPurple", { 114.0 / 255, 46.0 / 255, 209.0 / 255, 1.0 } },
    { "magenta", { 235.0 / 255, 48.0 / 255, 150.0 / 255, 1.0 } },
    { "sunRed", { 243.0 / 255, 34.0 / 255, 45.0 / 255, 1.0 } },
    { "sunsetOrange", { 246.0 / 255, 140.0 / 255, 39.0 / 255, 1.0 } },
    { "dustGold", { 255.0 / 255, 217.0 / 255, 102.0 / 255, 1.0 } },
    { "polarGreen", { 82.0 / 255, 196.0 / 255, 41.0 / 255, 1.0 } },
};

static struct ukui_settings *settings = NULL;

static bool is_schema_installed(const char *schema_id)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = g_settings_schema_source_lookup(source, schema_id, TRUE);

    if (schema) {
        g_settings_schema_unref(schema);
    }

    return schema;
}

static void handle_cursor_settings_changed(GSettings *mouse, const char *key)
{
    if (strcmp(key, cursor_theme_key) == 0) {
        free(settings->cursor.theme);
        settings->cursor.theme = g_settings_get_string(mouse, key);
    } else if (strcmp(key, cursor_size_key) == 0) {
        settings->cursor.size = g_settings_get_int(mouse, key);
    } else {
        return;
    }

    input_set_all_cursor(settings->cursor.theme, settings->cursor.size);
}

static void style_name_changed(GSettings *style, const char *key)
{
    free(settings->style.style_name);
    settings->style.style_name = g_settings_get_string(style, key);
    if (!strcmp(settings->style.style_name, UKUI_THEME_LIGHT)) {
        theme_manager_set_theme(THEME_TYPE_LIGHT);
    } else if (!strcmp(settings->style.style_name, UKUI_THEME_DARK)) {
        theme_manager_set_theme(THEME_TYPE_DARK);
    }
}

static void icon_theme_changed(GSettings *style, const char *key)
{
    free(settings->style.icon_theme);
    settings->style.icon_theme = g_settings_get_string(style, key);
    theme_manager_set_icon_theme(settings->style.icon_theme);
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
    theme_manager_set_font(settings->style.font_name, atoi(settings->style.font_size));
}

static int32_t get_color_int(float *rgba)
{
    int32_t color = (int)(rgba[0] * 255) << 16;
    color |= (int)(rgba[1] * 255) << 8;
    color |= (int)(rgba[2] * 255);
    return color;
}

static void theme_manager_apply_accent_color(void)
{
    for (size_t i = 0; i < sizeof(ukui_accent_colors) / sizeof(struct ukui_accent_color); i++) {
        if (strcmp(settings->style.accent_color, ukui_accent_colors[i].name) == 0) {
            uint32_t color = get_color_int(ukui_accent_colors[i].accent_color);
            theme_manager_set_accent_color(color);
            return;
        }
    }
}

static void accent_color_changed(GSettings *style, const char *key)
{
    free(settings->style.accent_color);
    settings->style.accent_color = g_settings_get_string(style, key);
    theme_manager_apply_accent_color();
}

static void window_radius_changed(GSettings *style, const char *key)
{
    settings->style.window_radius = g_settings_get_int(style, key);
    theme_manager_set_corner_radius(settings->style.window_radius);
}

static void menu_transparency_changed(GSettings *style, const char *key)
{
    settings->style.menu_transparency = g_settings_get_int(style, key);
    theme_manager_set_opacity(settings->style.menu_transparency);
}

static void handle_style_settings_changed(GSettings *style, const char *key)
{
    if (strcmp(key, style_name_key) == 0) {
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

static bool cursor_schema_settings(void)
{
    if (!is_schema_installed(cursor_schema)) {
        return false;
    }

    settings->cursor.settings = g_settings_new(cursor_schema);
    settings->cursor.theme = g_settings_get_string(settings->cursor.settings, cursor_theme_key);
    settings->cursor.size = g_settings_get_int(settings->cursor.settings, cursor_size_key);
    if (settings->cursor.theme && settings->cursor.size > 0) {
        input_set_all_cursor(settings->cursor.theme, settings->cursor.size);
    }

    return true;
}

static bool style_schema_settings(void)
{
    if (!is_schema_installed(style_schema)) {
        return false;
    }

    settings->style.settings = g_settings_new(style_schema);

    settings->style.style_name = g_settings_get_string(settings->style.settings, style_name_key);
    if (settings->style.style_name) {
        if (!strcmp(settings->style.style_name, UKUI_THEME_LIGHT)) {
            theme_manager_set_theme(THEME_TYPE_LIGHT);
        } else if (!strcmp(settings->style.style_name, UKUI_THEME_DARK)) {
            theme_manager_set_theme(THEME_TYPE_DARK);
        }
    }

    settings->style.icon_theme = g_settings_get_string(settings->style.settings, icon_theme_key);
    if (settings->style.icon_theme) {
        theme_manager_set_icon_theme(settings->style.icon_theme);
    }

    settings->style.accent_color =
        g_settings_get_string(settings->style.settings, accent_color_key);
    if (settings->style.accent_color) {
        theme_manager_apply_accent_color();
    }

    settings->style.font_name = g_settings_get_string(settings->style.settings, font_name_key);
    settings->style.font_size = g_settings_get_string(settings->style.settings, font_size_key);
    if (settings->style.font_name && settings->style.font_size) {
        theme_manager_set_font(settings->style.font_name, atoi(settings->style.font_size));
    }

    settings->style.window_radius = g_settings_get_int(settings->style.settings, window_radius_key);
    if (settings->style.window_radius >= 0) {
        theme_manager_set_corner_radius(settings->style.window_radius);
    }

    settings->style.menu_transparency =
        g_settings_get_int(settings->style.settings, menu_transparency_key);
    if (settings->style.menu_transparency >= 0) {
        theme_manager_set_opacity(settings->style.menu_transparency);
    }

    return true;
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&settings->destroy.link);

    g_object_unref(settings->cursor.settings);
    g_object_unref(settings->style.settings);

    free(settings->cursor.theme);
    free(settings->style.style_name);
    free(settings->style.icon_theme);
    free(settings->style.accent_color);
    free(settings->style.font_name);
    free(settings->style.font_size);

    free(settings);
}

bool ukui_gsettings_create(struct config_manager *config_manager)
{
    settings = calloc(1, sizeof(struct ukui_settings));
    if (!settings) {
        return false;
    }

    bool has_gsettings = cursor_schema_settings();
    has_gsettings |= style_schema_settings();

    if (!has_gsettings) {
        free(settings);
        settings = NULL;
        return false;
    }

    /* monitor dconf dbus notify */
    sd_bus_match_signal(config_manager->bus, NULL, NULL, "/ca/desrt/dconf/Writer/user",
                        "ca.desrt.dconf.Writer", "Notify", dconf_notify, NULL);

    settings->destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(config_manager->server->display, &settings->destroy);

    return true;
}
