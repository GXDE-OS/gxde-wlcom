// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <gio/gio.h>

#include "config_p.h"
#include "input/input.h"
#include "server.h"

#define EVENT_TIMEOUT (1000)

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

static void handle_style_settings_changed(GSettings *style, const char *key, void *data)
{
    if (strcmp(key, style_name_key) == 0) {
        free(settings->style.style_name);
        settings->style.style_name = g_settings_get_string(style, key);
    } else if (strcmp(key, icon_theme_key) == 0) {
        free(settings->style.icon_theme);
        settings->style.icon_theme = g_settings_get_string(style, key);
    } else if (strcmp(key, font_name_key) == 0) {
        free(settings->style.font_name);
        settings->style.font_name = g_settings_get_string(style, key);
    } else if (strcmp(key, font_size_key) == 0) {
        free(settings->style.font_size);
        settings->style.font_size = g_settings_get_string(style, key);
    } else if (strcmp(key, accent_color_key) == 0) {
        free(settings->style.accent_color);
        settings->style.accent_color = g_settings_get_string(style, key);
    }
}

bool ukui_gsettings_create(struct config_manager *config_manager)
{
    settings = calloc(1, sizeof(struct ukui_settings));
    if (!settings) {
        return false;
    }

    bool has_cursor = is_schema_installed(cursor_schema);
    if (has_cursor) {
        settings->cursor.settings = g_settings_new(cursor_schema);
        g_signal_connect(settings->cursor.settings, "changed",
                         G_CALLBACK(handle_cursor_settings_changed), NULL);

        settings->cursor.theme = g_settings_get_string(settings->cursor.settings, cursor_theme_key);
        settings->cursor.size = g_settings_get_int(settings->cursor.settings, cursor_size_key);
        input_set_all_cursor(settings->cursor.theme, settings->cursor.size);
    }

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
    }

    if (!has_cursor && !has_style) {
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
