/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This file is part of gxde-daemon.
 *
 * gxde-daemon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gxde-daemon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gxde-daemon.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <gio/gio.h>
#include <stdbool.h>
#include <string.h>

#include "config.h"

struct gtk_theme_setting {
    const char *schema;
    const char *key;
};

static const struct gtk_theme_setting gtk_theme_settings[] = {
    { "org.ukui.style", "widget-theme-name" },
    { "org.gnome.desktop.interface", "gtk-theme" },
};

static const struct gtk_theme_setting gtk_decoration_layout = {
    "org.gnome.desktop.wm.preferences", "button-layout"
};

static bool directory_has_gtk_theme(const char* directory) {
    static const char* files[] = {
        "gtk-2.0/gtkrc",
        "gtk-3.0/gtk.css",
        "gtk-4.0/gtk.css",
    };

    for (size_t i = 0; i < G_N_ELEMENTS(files); i++) {
        g_autofree char* path = g_build_filename(directory, files[i], NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
            return true;
        }
    }

    return false;
}

static bool gtk_theme_is_installed(const char* name) {
    if (!name || !*name || strchr(name, G_DIR_SEPARATOR)) {
        return false;
    }

    const char* data_home = g_get_user_data_dir();
    g_autofree char* user_theme = g_build_filename(data_home, "themes", name, NULL);
    if (directory_has_gtk_theme(user_theme)) {
        return true;
    }

    g_autofree char* legacy_theme = g_build_filename(g_get_home_dir(), ".themes", name, NULL);
    if (directory_has_gtk_theme(legacy_theme)) {
        return true;
    }

    const char* const* data_dirs = g_get_system_data_dirs();
    for (size_t i = 0; data_dirs[i]; i++) {
        g_autofree char* system_theme = g_build_filename(data_dirs[i], "themes", name, NULL);
        if (directory_has_gtk_theme(system_theme)) {
            return true;
        }
    }

    return false;
}

static bool set_schema_string(GSettingsSchemaSource* source,
        const struct gtk_theme_setting *setting, const char *value,
        bool* available) {
    g_autoptr(GSettingsSchema) schema =
        g_settings_schema_source_lookup(source, setting->schema, TRUE);

    if (!schema || !g_settings_schema_has_key(schema, setting->key)) {
        return true;
    }

    *available = true;
    g_autoptr(GSettings) settings = g_settings_new_full(schema, NULL, NULL);
    if (!g_settings_is_writable(settings, setting->key)) {
        return false;
    }

    return g_settings_set_string(settings, setting->key, value);
}

bool config_set_gtk_theme(const char* name) {
    if (!gtk_theme_is_installed(name)) {
        return false;
    }

    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (!source) {
        return false;
    }

    bool available = false;
    bool success = true;
    for (size_t i = 0; i < G_N_ELEMENTS(gtk_theme_settings); i++) {
        if (!set_schema_string(source, &gtk_theme_settings[i], name, &available)) {
            success = false;
        }
    }

    if (available && success) {
        g_settings_sync();
    }

    return available && success;
}

bool config_set_gtk_decoration_layout(bool minimize, bool maximize, bool close) {
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (!source) {
        return false;
    }

    char layout[64] = ":";
    bool is_first_btn = true;
    if (minimize) {
        g_strlcat(layout, "minimize", sizeof(layout));
        is_first_btn = false;
    }

    if (maximize) {
        g_strlcat(layout, is_first_btn ? "maximize" : ",maximize", sizeof(layout));
        is_first_btn = false;
    }

    if (close) {
        g_strlcat(layout, is_first_btn ? "close" : ",close", sizeof(layout));
    }

    bool available = false;
    bool success = set_schema_string(source, &gtk_decoration_layout,
        layout, &available);

    if (available && success) {
        g_settings_sync();
    }

    return available && success;
}
