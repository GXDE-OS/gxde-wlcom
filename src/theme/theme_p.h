// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _THEME_P_H_
#define _THEME_P_H_

#include "theme.h"

#define ICONPATH "~/.icons:~/.local/share/icons:/usr/share/icons"
#define APPPATH "~/.local/share/applications:/usr/local/share/applications:/usr/share/applications"
#define PIXMAPPATH "/usr/share/pixmaps"

#define DEFAULT_ICON_THEME_NAME "hicolor"

struct theme_buffer {
    struct wl_list link;
    float scale;
    struct wlr_buffer *buf;
};

struct icon_buffer {
    struct wl_list link;
    struct wlr_buffer *buffer;
    float scale;
};

struct icon_png {
    struct wl_list link;
    char *path;
    uint32_t width;
    uint32_t height;
    uint32_t scale;
};

struct icon {
    struct wl_list link;
    struct wl_list buffers; // struct icon_buffer
    struct wl_list pngs;    // struct icon_png
    char *name;
    char *svg;
    char *xpm_path;
};

struct icon_subdir {
    struct wl_list link;
    char *subdir;
};

struct icon_theme {
    struct wl_list link;
    struct wl_list parents_theme; // struct icon_theme
    char *name;
    struct wl_list icons;        // struct icon
    struct wl_list icons_subdir; // struct icon_subdir
};

struct theme_override {
    /* font config override by dbus */
    char *font_name;
    int32_t font_size;
    /* default to -1 */
    int32_t accent_color;
    int32_t corner_radius;
    int32_t opacity;
};

struct desktop_info {
    struct wl_list link;
    char *app_id;
    char *icon_name;
};

struct theme_manager {
    struct wl_event_source *timer;
    struct wl_list themes;
    struct theme *current;
    struct theme_override override;
    struct config *config;

    /* desktop infos*/
    struct wl_list desktop_infos; // struct desktop_info
    /* current icon theme */
    struct icon_theme *icon_theme;
    /* hicolor icon theme */
    struct icon_theme *hicolor_theme;
    /* pixmaps icons */
    struct wl_list pixmaps_icons; // struct icon
    /* specific icons */
    struct wl_list specific_icons; // struct icon
    /* fallback icon */
    struct icon *fallback_icon;

    struct {
        struct wl_signal update;
        struct wl_signal icon_update;
    } events;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

bool theme_manager_config_init(struct theme_manager *manager);

const char *theme_manager_read_config(struct theme_manager *manager);

void theme_manager_write_config(struct theme_manager *manager, const char *name);

const char *theme_manager_read_icon_config(struct theme_manager *manager);

void theme_manager_write_icon_config(struct theme_manager *manager, const char *name);

const char *theme_name_from_theme_type(enum theme_type theme_type);

struct icon_theme *icon_theme_load(const char *name);

struct icon *icon_fallback_create(void);

void icon_destroy(struct icon *icon);

void icon_load_desktop(struct wl_list *desktop_infos);

void desktop_infos_destroy(struct wl_list *desktop_infos);

void icon_load_pixmaps_path(struct wl_list *pixmap_icons);

void pixmaps_icon_destroy(struct wl_list *pixmap_icons);

void icon_theme_destroy(struct icon_theme *theme);

struct icon *icon_theme_get_icon(struct icon_theme *theme, const char *name, bool search_parents);

bool icon_need_reload(const char *path, struct icon_theme *theme, time_t threshold);

struct icon *icon_create(struct icon_theme *theme, const char *path, const char *full_name);

#endif /* _THEME_P_H */
