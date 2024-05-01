// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <gio/gio.h>

#include "config_p.h"
#include "input/input.h"
#include "server.h"
#include "theme.h"

#define EVENT_TIMEOUT (1000)

static struct kwin_accent_color {
    char *name;
    float accent_color[4];
} kwin_accent_colors[] = {
    { "daybreakBlue", { 55.0 / 255, 144.0 / 255, 250.0 / 255, 1.0 } },
    { "jamPurple", { 114.0 / 255, 46.0 / 255, 209.0 / 255, 1.0 } },
    { "magenta", { 235.0 / 255, 48.0 / 255, 150.0 / 255, 1.0 } },
    { "sunRed", { 243.0 / 255, 34.0 / 255, 45.0 / 255, 1.0 } },
    { "sunsetOrange", { 246.0 / 255, 140.0 / 255, 39.0 / 255, 1.0 } },
    { "dustGold", { 255.0 / 255, 217.0 / 255, 102.0 / 255, 1.0 } },
    { "polarGreen", { 82.0 / 255, 196.0 / 255, 41.0 / 255, 1.0 } },
};

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
    } style;

    struct wl_event_source *timer;
    struct wl_listener destroy;
};

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

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&settings->destroy.link);
    wl_event_source_remove(settings->timer);

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

static int handle_gsettings_event(void *data)
{
    // TODO: cache the changes, iteration all pending event and apply changes by mask
    g_main_context_iteration(NULL, FALSE);
    wl_event_source_timer_update(settings->timer, EVENT_TIMEOUT);
    return 0;
}

static void handle_cursor_settings_changed(GSettings *mouse, const char *key, void *data)
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
    theme_manager_set_theme(settings->style.style_name);
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

static void accent_color_effect(void)
{
    for (size_t i = 0; i < sizeof(kwin_accent_colors) / sizeof(struct kwin_accent_color); i++) {
        if (strcmp(settings->style.accent_color, kwin_accent_colors[i].name) == 0) {
            uint32_t color = get_color_int(kwin_accent_colors[i].accent_color);
            theme_manager_set_accent_color(color);
            return;
        }
    }
}

static void accent_color_changed(GSettings *style, const char *key)
{
    free(settings->style.accent_color);
    settings->style.accent_color = g_settings_get_string(style, key);
    accent_color_effect();
}

static void window_radius_changed(GSettings *style, const char *key)
{
    settings->style.window_radius = g_settings_get_int(style, key);
    theme_manager_set_corner_radius(settings->style.window_radius);
}

static void handle_style_settings_changed(GSettings *style, const char *key, void *data)
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
    }
}

static bool cursor_schema_settings(void)
{
    bool has_cursor = is_schema_installed(cursor_schema);
    if (has_cursor) {
        settings->cursor.settings = g_settings_new(cursor_schema);
        g_signal_connect(settings->cursor.settings, "changed",
                         G_CALLBACK(handle_cursor_settings_changed), NULL);

        settings->cursor.theme = g_settings_get_string(settings->cursor.settings, cursor_theme_key);
        settings->cursor.size = g_settings_get_int(settings->cursor.settings, cursor_size_key);
        input_set_all_cursor(settings->cursor.theme, settings->cursor.size);
    }
    return has_cursor;
}

static void style_schema_config_effect(void)
{
    if (settings->style.style_name) {
        theme_manager_set_theme(settings->style.style_name);
    }
    if (settings->style.icon_theme) {
        theme_manager_set_icon_theme(settings->style.icon_theme);
    }
    if (settings->style.accent_color) {
        theme_manager_set_theme(settings->style.style_name);
    }
    if (settings->style.font_name && settings->style.font_size) {
        accent_color_effect();
    }
    if (settings->style.window_radius >= 0) {
        theme_manager_set_corner_radius(settings->style.window_radius);
    }
}

static bool style_schema_settings(void)
{
    bool has_style = is_schema_installed(style_schema);
    if (has_style) {
        settings->style.settings = g_settings_new(style_schema);
        g_signal_connect(settings->style.settings, "changed",
                         G_CALLBACK(handle_style_settings_changed), NULL);

        settings->style.style_name =
            g_settings_get_string(settings->style.settings, style_name_key);
        settings->style.icon_theme =
            g_settings_get_string(settings->style.settings, icon_theme_key);
        settings->style.accent_color =
            g_settings_get_string(settings->style.settings, accent_color_key);
        settings->style.font_name = g_settings_get_string(settings->style.settings, font_name_key);
        settings->style.font_size = g_settings_get_string(settings->style.settings, font_size_key);
        settings->style.window_radius =
            g_settings_get_int(settings->style.settings, window_radius_key);
        style_schema_config_effect();
    }
    return has_style;
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

    /* workaourd to process glib event loop in wayland server */
    struct wl_event_loop *loop = wl_display_get_event_loop(config_manager->server->display);
    settings->timer = wl_event_loop_add_timer(loop, handle_gsettings_event, NULL);
    wl_event_source_timer_update(settings->timer, EVENT_TIMEOUT);

    settings->destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(config_manager->server->display, &settings->destroy);

    return true;
}
