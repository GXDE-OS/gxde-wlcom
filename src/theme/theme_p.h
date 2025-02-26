// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _THEME_P_H_
#define _THEME_P_H_

#include "theme.h"

#define FALLBACK_ICON_THEME_NAME "hicolor"

struct theme_buffer {
    struct wl_list link;
    struct wlr_buffer *buffer;
    float scale;
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
    struct widget_theme *(*load_widget_theme)(const char *name, enum theme_type type);

    struct {
        struct wl_signal update;
        struct wl_signal icon_update;
    } events;

    struct config *config;
    struct wl_list fallback_icon;

    struct icon_manager *icon;
    struct {
        bool (*set_icon_theme)(struct icon_manager *manager, const char *name);
        struct icon *(*get_icon)(struct icon_manager *manager, const char *name);
        const char *(*get_icon_name)(struct icon *icon);
        struct wlr_buffer *(*get_icon_buffer)(struct icon *icon, int size, float scale);
    } icon_impl;

    struct server *server;
    struct wl_listener server_destroy;
};

bool theme_manager_config_init(struct theme_manager *manager);

enum theme_type theme_manager_read_config(struct theme_manager *manager);

void theme_manager_write_config(struct theme_manager *manager, enum theme_type name);

const char *theme_manager_read_icon_config(struct theme_manager *manager);

void theme_manager_write_icon_config(struct theme_manager *manager, const char *name);

#if 0 // HAVE_THEME_ICON
struct icon_manager *icon_manager_create(struct theme_manager *manager);
#else
static __attribute__((unused)) inline struct icon_manager *
icon_manager_create(struct theme_manager *manager)
{
    return NULL;
}
#endif

#endif /* _THEME_P_H */
