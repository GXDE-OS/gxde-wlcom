/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -----------------------------------------------------------------------------
 * Follows the system dark/light preference and publishes it as the
 * treeland_personalization_appearance_context_v1 window theme type, which
 * DTK5/6 Wayland clients map to the "deepin" / "deepin-dark" theme.
 *
 * Two sources are watched:
 *  - org.gnome.desktop.interface color-scheme (prefer-dark / prefer-light / default)
 *  - com.deepin.dde.appearance gtk-theme (GXDE appearance, dark when the name
 *    contains "dark")
 * The one changed most recently wins; at startup an explicit color-scheme
 * wins over gtk-theme. This matches the rule used by dtk2widget.
 *
 * Wlcom does not run a GLib main loop, so changes are picked up from dconf's
 * Notify signal on the session bus, the same way ukui_gsettings.c does.
 */

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kywc/log.h>

#include "treeland-personalization-manager-v1-protocol.h"
#include "util/dbus.h"
#include "view_p.h"

enum color_scheme {
    COLOR_SCHEME_NONE = 0,
    COLOR_SCHEME_DARK,
    COLOR_SCHEME_LIGHT,
};

enum color_scheme_source {
    SOURCE_GNOME,
    SOURCE_DEEPIN,
};

static const char *gnome_schema = "org.gnome.desktop.interface";
static const char *gnome_key = "color-scheme";
static const char *gnome_path = "/org/gnome/desktop/interface/color-scheme";

static const char *deepin_schema = "com.deepin.dde.appearance";
static const char *deepin_key = "gtk-theme";
static const char *deepin_path = "/com/deepin/dde/appearance/gtk-theme";

static struct {
    GSettings *gnome;
    GSettings *deepin;
    enum color_scheme gnome_scheme;
    enum color_scheme deepin_scheme;
    enum color_scheme_source last_source;
} *color_scheme = NULL;

static GSettings *settings_new_with_key(const char *schema_id, const char *key)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (!source) {
        return NULL;
    }

    g_autoptr(GSettingsSchema) schema = g_settings_schema_source_lookup(source, schema_id, TRUE);
    if (!schema || !g_settings_schema_has_key(schema, key)) {
        return NULL;
    }

    return g_settings_new_full(schema, NULL, NULL);
}

static enum color_scheme read_gnome_scheme(void)
{
    if (!color_scheme->gnome) {
        return COLOR_SCHEME_NONE;
    }

    g_autofree char *value = g_settings_get_string(color_scheme->gnome, gnome_key);
    if (!strcmp(value, "prefer-dark")) {
        return COLOR_SCHEME_DARK;
    }
    if (!strcmp(value, "prefer-light")) {
        return COLOR_SCHEME_LIGHT;
    }
    return COLOR_SCHEME_NONE;
}

static enum color_scheme read_deepin_scheme(void)
{
    if (!color_scheme->deepin) {
        return COLOR_SCHEME_NONE;
    }

    g_autofree char *value = g_settings_get_string(color_scheme->deepin, deepin_key);
    if (!value || !*value) {
        return COLOR_SCHEME_NONE;
    }

    g_autofree char *lower = g_ascii_strdown(value, -1);
    return strstr(lower, "dark") ? COLOR_SCHEME_DARK : COLOR_SCHEME_LIGHT;
}

static enum color_scheme current_scheme(void)
{
    enum color_scheme last = color_scheme->last_source == SOURCE_GNOME
                                 ? color_scheme->gnome_scheme
                                 : color_scheme->deepin_scheme;
    if (last != COLOR_SCHEME_NONE) {
        return last;
    }
    if (color_scheme->gnome_scheme != COLOR_SCHEME_NONE) {
        return color_scheme->gnome_scheme;
    }
    return color_scheme->deepin_scheme;
}

static void apply_scheme(void)
{
    uint32_t type;
    switch (current_scheme()) {
    case COLOR_SCHEME_DARK:
        type = TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_THEME_TYPE_DARK;
        break;
    case COLOR_SCHEME_LIGHT:
        type = TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_THEME_TYPE_LIGHT;
        break;
    default:
        type = TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_THEME_TYPE_AUTO;
        break;
    }

    kywc_log(KYWC_DEBUG, "(Treeland Shim) Color scheme: window theme type %u", type);
    treeland_personalization_set_window_theme_type(type);
}

static void update_source(enum color_scheme *scheme, enum color_scheme value,
                          enum color_scheme_source source)
{
    if (*scheme == value) {
        return;
    }

    *scheme = value;
    color_scheme->last_source = source;
    apply_scheme();
}

/* changed is a full dconf key, or a directory (ending in '/') when it was reset */
static bool dconf_path_matches(const char *changed, const char *path)
{
    size_t len = strlen(changed);
    if (len > 0 && changed[len - 1] == '/') {
        return strncmp(path, changed, len) == 0;
    }
    return strcmp(path, changed) == 0;
}

static int dconf_notify(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *prefix;
    CK(sd_bus_message_read_basic(msg, 's', &prefix));
    CK(sd_bus_message_enter_container(msg, 'a', "s"));

    const char *change;
    char key[256];
    int ret = 0;

    while (true) {
        ret = sd_bus_message_read(msg, "s", &change);
        if (ret < 0) {
            return ret;
        } else if (ret == 0) {
            break;
        }
        snprintf(key, sizeof(key), "%s%s", prefix, change);

        if (dconf_path_matches(key, gnome_path)) {
            update_source(&color_scheme->gnome_scheme, read_gnome_scheme(), SOURCE_GNOME);
        }
        if (dconf_path_matches(key, deepin_path)) {
            update_source(&color_scheme->deepin_scheme, read_deepin_scheme(), SOURCE_DEEPIN);
        }
    }

    return 0;
}

bool treeland_color_scheme_create(void)
{
    if (color_scheme) {
        return true;
    }

    color_scheme = calloc(1, sizeof(*color_scheme));
    if (!color_scheme) {
        return false;
    }

    color_scheme->gnome = settings_new_with_key(gnome_schema, gnome_key);
    color_scheme->deepin = settings_new_with_key(deepin_schema, deepin_key);
    if (!color_scheme->gnome && !color_scheme->deepin) {
        kywc_log(KYWC_INFO, "(Treeland Shim) Color scheme: no color scheme settings available");
        free(color_scheme);
        color_scheme = NULL;
        return false;
    }

    color_scheme->gnome_scheme = read_gnome_scheme();
    color_scheme->deepin_scheme = read_deepin_scheme();
    color_scheme->last_source =
        color_scheme->gnome_scheme != COLOR_SCHEME_NONE ? SOURCE_GNOME : SOURCE_DEEPIN;
    apply_scheme();

    dbus_match_signal(NULL, "/ca/desrt/dconf/Writer/user", "ca.desrt.dconf.Writer", "Notify",
                      dconf_notify, NULL);

    return true;
}
