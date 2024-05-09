// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _THEME_H_
#define _THEME_H_

#include <wayland-server-core.h>

enum justification {
    JUSTIFY_LEFT = 0,
    JUSTIFY_CENTER,
    JUSTIFY_RIGHT,
};

enum theme_buffer_type {
    BUTTON_MINIMIZE_HOVER = 0,
    BUTTON_MAXIMIZE_HOVER,
    BUTTON_RESTORE_HOVER,
    BUTTON_CLOSE_HOVER,
    BUTTON_MINIMIZE,
    BUTTON_MAXIMIZE,
    BUTTON_RESTORE,
    BUTTON_CLOSE,
    CORNER_TOP_LEFT_ACTIVE,
    CORNER_TOP_LEFT_INACTIVE,
    CORNER_TOP_RIGHT_ACTIVE,
    CORNER_TOP_RIGHT_INACTIVE,
};

struct server;
struct wlr_fbox;
struct wlr_buffer;

struct theme {
    struct wl_list link;
    const char *theme_name;
    bool builtin;

    int corner_radius;
    /* font */
    const char *font_name;
    int font_size;

    /* border color */
    float active_border_color[4];
    float inactive_border_color[4];

    /* background color */
    float active_bg_color[4];
    float inactive_bg_color[4];

    /* text color */
    float active_text_color[4];
    float inactive_text_color[4];
    enum justification text_justify;

    float accent_color[4];

    /* icon size */
    int button_width;
    int icon_size;

    /* not changed or ignored parameter */
    int border_width;
    int title_height;
    int shadow_border;

    /* button svg string */
    const char *button_svg;

    struct wl_list scaled_buffers;
};

struct theme_manager *theme_manager_create(struct server *server);

void theme_manager_add_update_listener(struct wl_listener *listener);

void theme_manager_add_icon_update_listener(struct wl_listener *listener);

struct theme *theme_manager_get_current(void);

bool theme_manager_set_theme(const char *name);

bool theme_manager_set_font(const char *name, int size);

bool theme_manager_set_accent_color(int32_t color);

bool theme_manager_set_icon_theme(const char *icon_theme_name);

bool theme_manager_set_corner_radius(int32_t radius);

struct wlr_buffer *theme_buffer_load(struct theme *theme, float scale, enum theme_buffer_type type,
                                     struct wlr_fbox *src);

struct wlr_buffer *theme_icon_load(const char *app_id, float scale);

const char *theme_icon_name(const char *app_id);

#endif /* _THEME_H_ */
