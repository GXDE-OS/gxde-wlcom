// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _THEME_P_H_
#define _THEME_P_H_

#include "theme.h"

#define ICONPATH "~/.icons:~/.local/share/icons:/usr/share/icons:" EXTRA_ICON_PATH
#define APPPATH                                                                                    \
    "~/.local/share/applications:/usr/local/share/applications:/usr/share/applications:/etc/xdg/"  \
    "autostart:" EXTRA_APPS_PATH
#define PIXMAPPATH "/usr/share/pixmaps"

#define DEFAULT_ICON_THEME_NAME "default"
#define FALLBACK_ICON_THEME_NAME "hicolor"

struct theme_buffer {
    struct wl_list link;
    struct wlr_buffer *buffer;
    float scale;
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

struct icon_pair {
    struct wl_list link;
    char *app_id;
    struct icon *icon;
};

struct icon_theme {
    struct wl_list link;
    char *name;
    struct wl_list icons;   // struct icon
    struct wl_list parents; // struct icon_theme
    struct wl_list subdirs; // struct icon_subdir
};

struct desktop_info {
    struct wl_list link;
    char *app_id;
    char *icon_name;
    char *exec_name;
    char *startup_name;
};

struct widget_theme {
    const char *name;
    enum theme_type type;
    bool builtin;

    /* border color */
    float active_border_color[4];
    float inactive_border_color[4];

    /* background color */
    float active_bg_color[4];
    float inactive_bg_color[4];

    /* text color */
    float active_text_color[4];
    float inactive_text_color[4];

    /* default accent color, may override by global */
    float accent_color[4];

    /* modal mask color */
    float modal_mask_color[4];

    /**
     * minimize, maximize, restore and close
     * in different state: active/inactive, hover, click
     */
    const char *button_svg;
};

struct global_theme {
    /* font config */
    char *font_name;
    int32_t font_size;
    /* default to -1 */
    int32_t accent_color;
    int32_t corner_radius;
    int32_t opacity;
};

struct theme_manager {
    struct theme theme;
    struct global_theme global;

    /* cache of appid icon */
    struct wl_list icon_pairs; // struct icon_pair.link
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
    /* timer to check for changes in icon-related files. */
    struct wl_event_source *timer;

    struct {
        struct wl_signal update;
        struct wl_signal icon_update;
    } events;

    struct config *config;

    struct server *server;
    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

bool theme_manager_config_init(struct theme_manager *manager);

enum theme_type theme_manager_read_config(struct theme_manager *manager);

void theme_manager_write_config(struct theme_manager *manager, enum theme_type name);

const char *theme_manager_read_icon_config(struct theme_manager *manager);

void theme_manager_write_icon_config(struct theme_manager *manager, const char *name);

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
