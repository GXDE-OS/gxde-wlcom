// SPDX-FileCopyrightText: 2026 CharOfString <root@charofstring.cc>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>
#include <xcb/xcb.h>

#include "input/input.h"
#include "input/seat.h"
#include "theme.h"
#include "util/string.h"
#include "xwayland_p.h"

#define XSETTINGS_ATOM_NAME "_XSETTINGS_SETTINGS"
#define XSETTINGS_SELECTION_NAME "_XSETTINGS_S%d"
#define XSETTINGS_MANAGER_NAME "gxde-wlcom-xsettings"
#define XSETTINGS_FIXED_BASE_DPI (96 * 1024)

enum xsettings_type {
    XSETTINGS_TYPE_INTEGER = 0,
    XSETTINGS_TYPE_STRING = 1,
    XSETTINGS_TYPE_COLOR = 2,
};

struct xsettings_window {
    struct wl_list link;
    xcb_window_t window;
    xcb_atom_t selection;
};

struct xsettings_manager {
    struct xwayland_server *xwayland;
    struct wl_list windows;
    xcb_atom_t settings_atom;
    xcb_atom_t manager_atom;
    uint32_t serial;
};

struct xsettings_buffer {
    uint8_t *data;
    size_t len;
    size_t cap;
};

static bool buffer_reserve(struct xsettings_buffer *buffer, size_t add)
{
    if (buffer->len + add <= buffer->cap) {
        return true;
    }

    size_t cap = buffer->cap ? buffer->cap * 2 : 256;
    while (cap < buffer->len + add) {
        cap *= 2;
    }

    uint8_t *data = realloc(buffer->data, cap);
    if (!data) {
        return false;
    }

    buffer->data = data;
    buffer->cap = cap;
    return true;
}

static bool buffer_append(struct xsettings_buffer *buffer, const void *data, size_t len)
{
    if (!buffer_reserve(buffer, len)) {
        return false;
    }

    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    return true;
}

static bool buffer_append_zeroes(struct xsettings_buffer *buffer, size_t len)
{
    if (!buffer_reserve(buffer, len)) {
        return false;
    }

    memset(buffer->data + buffer->len, 0, len);
    buffer->len += len;
    return true;
}

static bool buffer_append_u8(struct xsettings_buffer *buffer, uint8_t value)
{
    return buffer_append(buffer, &value, sizeof(value));
}

static bool buffer_append_u16(struct xsettings_buffer *buffer, uint16_t value)
{
    return buffer_append(buffer, &value, sizeof(value));
}

static bool buffer_append_u32(struct xsettings_buffer *buffer, uint32_t value)
{
    return buffer_append(buffer, &value, sizeof(value));
}

static bool buffer_append_padding(struct xsettings_buffer *buffer, size_t size)
{
    size_t padding = (4 - (size % 4)) % 4;
    return buffer_append_zeroes(buffer, padding);
}

static bool xsettings_append_header(struct xsettings_buffer *buffer, uint32_t serial,
        uint32_t count)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t byte_order = XCB_IMAGE_ORDER_LSB_FIRST;
#else
    uint8_t byte_order = XCB_IMAGE_ORDER_MSB_FIRST;
#endif

    return buffer_append_u8(buffer, byte_order) && buffer_append_zeroes(buffer, 3) &&
           buffer_append_u32(buffer, serial) && buffer_append_u32(buffer, count);
}

static bool xsettings_append_entry_header(struct xsettings_buffer *buffer,
        enum xsettings_type type, const char *name, uint32_t serial)
{
    uint16_t name_len = strlen(name);

    return buffer_append_u8(buffer, type) && buffer_append_u8(buffer, 0) &&
        buffer_append_u16(buffer, name_len) && buffer_append(buffer, name, name_len) &&
        buffer_append_padding(buffer, name_len) && buffer_append_u32(buffer, serial);
}

static bool xsettings_append_string(struct xsettings_buffer *buffer, const char *name,
        const char *value, uint32_t serial)
{
    size_t value_len = strlen(value);

    return xsettings_append_entry_header(buffer, XSETTINGS_TYPE_STRING, name, serial) &&
        buffer_append_u32(buffer, value_len) && buffer_append(buffer, value, value_len) &&
        buffer_append_padding(buffer, value_len);
}

static bool xsettings_append_int(struct xsettings_buffer *buffer, const char *name, int32_t value,
        uint32_t serial)
{
    return xsettings_append_entry_header(buffer, XSETTINGS_TYPE_INTEGER, name, serial) &&
           buffer_append_u32(buffer, value);
}

static char *read_gsettings_string(const char *schema_id, const char *key)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (!source) {
        return NULL;
    }

    g_autoptr(GSettingsSchema) schema =
        g_settings_schema_source_lookup(source, schema_id, TRUE);
    if (!schema || !g_settings_schema_has_key(schema, key)) {
        return NULL;
    }

    g_autoptr(GSettings) settings = g_settings_new_full(schema, NULL, NULL);
    if (!settings) {
        return NULL;
    }

    char *value = g_settings_get_string(settings, key);
    if (!value || !*value) {
        g_free(value);
        return NULL;
    }

    return value;
}

static char *read_gtk_theme_name(void)
{
    char *name = read_gsettings_string("org.ukui.style", "widget-theme-name");
    if (name) {
        return name;
    }

    return read_gsettings_string("org.gnome.desktop.interface", "gtk-theme");
}

static char *read_icon_theme_name(void)
{
    char *name = read_gsettings_string("org.ukui.style", "icon-theme-name");
    if (name) {
        return name;
    }

    return read_gsettings_string("org.gnome.desktop.interface", "icon-theme");
}

static char *read_gtk_im_module(void)
{
    const char *module = getenv("GTK_IM_MODULE");
    if (module && *module) {
        return g_strdup(module);
    }

    char *name = read_gsettings_string("com.deepin.wrap.gnome.desktop.interface",
        "gtk-im-module");
    if (name) {
        return name;
    }

    return read_gsettings_string("org.gnome.desktop.interface", "gtk-im-module");
}

static const char *current_cursor_theme(struct seat *seat)
{
    if (seat && seat->state.cursor_theme && *seat->state.cursor_theme) {
        return seat->state.cursor_theme;
    }

    return "default";
}

static int32_t current_cursor_size(struct xwayland_server *xwayland, struct seat *seat)
{
    uint32_t cursor_size = seat && seat->state.cursor_size ? seat->state.cursor_size : 24;
    return (int32_t)(cursor_size * xwayland->scale);
}

static char *current_font_name(void)
{
    struct theme *theme = theme_manager_get_theme();
    const char *font_name = theme && theme->font_name ? theme->font_name : "Sans";
    int font_size = theme && theme->font_size > 0 ? theme->font_size : 10;

    return string_create("%s %d", font_name, font_size);
}

static bool build_xsettings_data(struct xsettings_manager *manager, struct xsettings_buffer *buffer,
        const char *gtk_theme, const char *icon_theme,
        const char *cursor_theme, int32_t cursor_size, const char *font_name,
        const char *gtk_im_module)
{
    float scale = manager->xwayland->scale > 0.0f ? manager->xwayland->scale : 1.0f;
    int32_t fixed_dpi = (int32_t)roundf(scale * XSETTINGS_FIXED_BASE_DPI);
    int32_t window_scaling_factor = (int32_t)floorf(scale);
    if (window_scaling_factor < 1) {
        window_scaling_factor = 1;
    }

    uint32_t serial = ++manager->serial;
    const uint32_t settings_count = gtk_im_module ? 15 : 14;

    return xsettings_append_header(buffer, serial, settings_count) &&
        xsettings_append_string(buffer, "Gtk/ThemeName", gtk_theme, serial) &&
        xsettings_append_string(buffer, "Net/ThemeName", gtk_theme, serial) &&
        xsettings_append_string(buffer, "Gtk/IconThemeName", icon_theme, serial) &&
        xsettings_append_string(buffer, "Net/IconThemeName", icon_theme, serial) &&
        xsettings_append_string(buffer, "Net/FallbackIconTheme", "hicolor", serial) &&
        xsettings_append_string(buffer, "Gtk/CursorThemeName", cursor_theme, serial) &&
        xsettings_append_string(buffer, "Xcursor/Theme", cursor_theme, serial) &&
        xsettings_append_int(buffer, "Gtk/CursorThemeSize", cursor_size, serial) &&
        xsettings_append_int(buffer, "Xcursor/Size", cursor_size, serial) &&
        xsettings_append_int(buffer, "Xft/DPI", fixed_dpi, serial) &&
        xsettings_append_int(buffer, "Gdk/UnscaledDPI", fixed_dpi, serial) &&
        xsettings_append_int(buffer, "Gdk/WindowScalingFactor", window_scaling_factor, serial) &&
        xsettings_append_string(buffer, "Gtk/FontName", font_name, serial) &&
        (!gtk_im_module ||
         xsettings_append_string(buffer, "Gtk/IMModule", gtk_im_module, serial)) &&
        xsettings_append_int(buffer, "Gtk/EnableAnimations", 1, serial);
}

static void update_xresources(struct xsettings_manager *manager, const char *gtk_theme,
        const char *icon_theme, const char *cursor_theme, int32_t cursor_size)
{
    float scale = manager->xwayland->scale > 0.0f ? manager->xwayland->scale : 1.0f;
    int32_t dpi = (int32_t)roundf(scale * 96.0f);
    char *resources = string_create(
        "Gtk/ThemeName: %s\n"
        "Gtk/IconThemeName: %s\n"
        "Net/ThemeName: %s\n"
        "Net/IconThemeName: %s\n"
        "Xft.dpi: %d\n"
        "Xcursor.size: %d\n"
        "Xcursor.theme: %s\n",
        gtk_theme, icon_theme, gtk_theme, icon_theme, dpi, cursor_size, cursor_theme);
    if (!resources) {
        return;
    }

    xcb_change_property(manager->xwayland->xcb_conn, XCB_PROP_MODE_REPLACE,
        manager->xwayland->screen->root, XCB_ATOM_RESOURCE_MANAGER,
        XCB_ATOM_STRING, 8, strlen(resources), resources);
    free(resources);
}

static xcb_atom_t intern_atom(xcb_connection_t *connection, const char *name)
{
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, 0, strlen(name), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, NULL);
    if (!reply) {
        kywc_log(KYWC_ERROR, "xsettings failed to resolve atom %s", name);
        return XCB_ATOM_NONE;
    }

    xcb_atom_t atom = reply->atom;
    free(reply);
    return atom;
}

static bool announce_manager(struct xsettings_manager *manager, xcb_screen_t *screen,
        xcb_atom_t selection, xcb_window_t window)
{
    xcb_client_message_event_t event = { 0 };
    event.response_type = XCB_CLIENT_MESSAGE;
    event.window = screen->root;
    event.type = manager->manager_atom;
    event.format = 32;
    event.data.data32[0] = XCB_CURRENT_TIME;
    event.data.data32[1] = selection;
    event.data.data32[2] = window;

    xcb_send_event(manager->xwayland->xcb_conn, false, screen->root,
                   XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char *)&event);
    return true;
}

static bool manage_screen(struct xsettings_manager *manager, int screen_index, xcb_screen_t *screen)
{
    xcb_connection_t *connection = manager->xwayland->xcb_conn;
    char selection_name[64];
    snprintf(selection_name, sizeof(selection_name), XSETTINGS_SELECTION_NAME, screen_index);

    xcb_atom_t selection = intern_atom(connection, selection_name);
    if (selection == XCB_ATOM_NONE) {
        return false;
    }

    xcb_window_t window = xcb_generate_id(connection);
    uint32_t values[] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, -1, -1, 1, 1, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_EVENT_MASK,
        values);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME,
        XCB_ATOM_STRING, 8, strlen(XSETTINGS_MANAGER_NAME),
        XSETTINGS_MANAGER_NAME);

    xcb_grab_server(connection);
    xcb_get_selection_owner_reply_t *owner_reply =
        xcb_get_selection_owner_reply(connection, xcb_get_selection_owner(connection, selection),
            NULL);
    if (owner_reply && owner_reply->owner != XCB_NONE) {
        kywc_log(KYWC_INFO, "xsettings replacing owner window %u for screen %d",
            owner_reply->owner, screen_index);
    }
    free(owner_reply);

    xcb_set_selection_owner(connection, window, selection, XCB_CURRENT_TIME);
    xcb_get_selection_owner_reply_t *reply =
        xcb_get_selection_owner_reply(connection, xcb_get_selection_owner(connection, selection),
                                      NULL);
    bool ok = reply && reply->owner == window;
    free(reply);
    xcb_ungrab_server(connection);
    if (!ok) {
        kywc_log(KYWC_ERROR, "xsettings failed to acquire %s", selection_name);
        xcb_destroy_window(connection, window);
        return false;
    }

    struct xsettings_window *xwin = calloc(1, sizeof(*xwin));
    if (!xwin) {
        xcb_destroy_window(connection, window);
        return false;
    }

    xwin->window = window;
    xwin->selection = selection;
    wl_list_insert(&manager->windows, &xwin->link);

    announce_manager(manager, screen, selection, window);
    kywc_log(KYWC_INFO, "xsettings manager owns %s on window %u", selection_name, window);
    return true;
}

void xwayland_xsettings_apply(struct xwayland_server *xwayland)
{
    if (!xwayland || !xwayland->xsettings || !xwayland->xcb_conn || !xwayland->screen) {
        return;
    }

    struct xsettings_manager *manager = xwayland->xsettings;
    struct seat *seat = xwayland->wlr_xwayland && xwayland->wlr_xwayland->seat
        ? seat_from_wlr_seat(xwayland->wlr_xwayland->seat)
        : input_manager_get_default_seat();

    g_autofree char *gtk_theme_setting = read_gtk_theme_name();
    g_autofree char *icon_theme_setting = read_icon_theme_name();
    const char *gtk_theme = gtk_theme_setting ? gtk_theme_setting : "Adwaita";
    const char *icon_theme = theme_manager_get_icon_theme();
    if (!icon_theme || !*icon_theme) {
        icon_theme = icon_theme_setting ? icon_theme_setting : "hicolor";
    }

    const char *cursor_theme = current_cursor_theme(seat);
    int32_t cursor_size = current_cursor_size(xwayland, seat);
    g_autofree char *font_name_alloc = current_font_name();
    const char *font_name = font_name_alloc ? font_name_alloc : "Sans 10";
    g_autofree char *gtk_im_module = read_gtk_im_module();

    struct xsettings_buffer buffer = { 0 };
    if (!build_xsettings_data(manager, &buffer, gtk_theme, icon_theme, cursor_theme, cursor_size,
                              font_name, gtk_im_module)) {
        kywc_log(KYWC_ERROR, "xsettings failed to build settings data");
        free(buffer.data);
        return;
    }

    xcb_grab_server(xwayland->xcb_conn);
    struct xsettings_window *window;
    wl_list_for_each(window, &manager->windows, link) {
        xcb_change_property(xwayland->xcb_conn, XCB_PROP_MODE_REPLACE, window->window,
            manager->settings_atom, manager->settings_atom, 8, buffer.len,
            buffer.data);
    }
    update_xresources(manager, gtk_theme, icon_theme, cursor_theme, cursor_size);
    xcb_ungrab_server(xwayland->xcb_conn);
    xcb_flush(xwayland->xcb_conn);

    free(buffer.data);
}

bool xwayland_xsettings_create(struct xwayland_server *xwayland)
{
    struct xsettings_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    manager->xwayland = xwayland;
    wl_list_init(&manager->windows);
    manager->settings_atom = intern_atom(xwayland->xcb_conn, XSETTINGS_ATOM_NAME);
    manager->manager_atom = intern_atom(xwayland->xcb_conn, "MANAGER");
    if (manager->settings_atom == XCB_ATOM_NONE || manager->manager_atom == XCB_ATOM_NONE) {
        free(manager);
        return false;
    }

    xwayland->xsettings = manager;

    const xcb_setup_t *setup = xcb_get_setup(xwayland->xcb_conn);
    xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(setup);
    int screen_count = xcb_setup_roots_length(setup);
    bool ok = false;
    for (int i = 0; i < screen_count && iterator.rem; i++) {
        ok = manage_screen(manager, i, iterator.data) || ok;
        xcb_screen_next(&iterator);
    }

    if (!ok) {
        xwayland_xsettings_destroy(xwayland);
        return false;
    }

    xwayland_xsettings_apply(xwayland);
    return true;
}

void xwayland_xsettings_destroy(struct xwayland_server *xwayland)
{
    if (!xwayland || !xwayland->xsettings) {
        return;
    }

    struct xsettings_manager *manager = xwayland->xsettings;
    struct xsettings_window *window, *tmp;
    wl_list_for_each_safe(window, tmp, &manager->windows, link) {
        if (xwayland->xcb_conn) {
            xcb_set_selection_owner(xwayland->xcb_conn, XCB_NONE, window->selection,
                XCB_CURRENT_TIME);
            xcb_destroy_window(xwayland->xcb_conn, window->window);
        }
        wl_list_remove(&window->link);
        free(window);
    }

    if (xwayland->xcb_conn) {
        xcb_flush(xwayland->xcb_conn);
    }
    xwayland->xsettings = NULL;
    free(manager);
}
