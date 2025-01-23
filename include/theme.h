// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _THEME_H_
#define _THEME_H_

#include <wayland-server-core.h>

enum theme_type {
    THEME_TYPE_UNDEFINED = -1,
    THEME_TYPE_DEFAULT,
    THEME_TYPE_LIGHT = THEME_TYPE_DEFAULT,
    THEME_TYPE_DARK,
};

enum justification {
    JUSTIFY_LEFT = 0,
    JUSTIFY_CENTER,
    JUSTIFY_RIGHT,
};

enum theme_buffer_type {
    THEME_BUFFER_TYPE_BUTTON_MINIMIZE = 0,
    THEME_BUFFER_TYPE_BUTTON_MAXIMIZE,
    THEME_BUFFER_TYPE_BUTTON_RESTORE,
    THEME_BUFFER_TYPE_BUTTON_CLOSE,
    THEME_BUFFER_TYPE_BUTTON_MINIMIZE_HOVER,
    THEME_BUFFER_TYPE_BUTTON_MAXIMIZE_HOVER,
    THEME_BUFFER_TYPE_BUTTON_RESTORE_HOVER,
    THEME_BUFFER_TYPE_BUTTON_CLOSE_HOVER,
    THEME_BUFFER_TYPE_BUTTON_MINIMIZE_CLICKED,
    THEME_BUFFER_TYPE_BUTTON_MAXIMIZE_CLICKED,
    THEME_BUFFER_TYPE_BUTTON_RESTORE_CLICKED,
    THEME_BUFFER_TYPE_BUTTON_CLOSE_CLICKED,
};

enum theme_update_mask {
    THEME_UPDATE_MASK_NONE = 0,
    /* theme type changed */
    THEME_UPDATE_MASK_TYPE = 1 << 0,
    /* font_name, font_size or text color changed */
    THEME_UPDATE_MASK_FONT = 1 << 1,
    /* border_color changed */
    THEME_UPDATE_MASK_BORDER_COLOR = 1 << 2,
    /* background_color changed */
    THEME_UPDATE_MASK_BACKGROUND_COLOR = 1 << 3,
    /* accent_color changed */
    THEME_UPDATE_MASK_ACCENT_COLOR = 1 << 4,
    /* corner_radius changed */
    THEME_UPDATE_MASK_CORNER_RADIUS = 1 << 5,
    /* opacity changed */
    THEME_UPDATE_MASK_OPACITY = 1 << 6,
    /* modal_mask_color changed */
    THEME_UPDATE_MASK_MODAL_MASK_COLOR = 1 << 7,
};

struct server;
struct wlr_fbox;
struct wlr_buffer;
struct icon;

struct theme_update_event {
    enum theme_type theme_type;
    uint32_t update_mask;
};

struct theme {
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

    /* modal mask color */
    float modal_mask_color[4];

    /* font */
    const char *font_name;
    int font_size;

    float accent_color[4];
    int corner_radius;
    int opacity; // 0 - 100

    enum justification text_justify;
    bool layout_is_right_to_left;
    bool text_is_right_align;
    bool ssd_need_maximize_button;

    /* icon size */
    int button_width;
    int icon_size;

    int border_width;
    int title_height;
    int subtitle_height;
    int shadow_border;

    /* button svg string */
    const char *button_svg;
    struct wl_list scaled_buffers;
};

struct theme_manager *theme_manager_create(struct server *server);

void theme_manager_add_update_listener(struct wl_listener *listener);

void theme_manager_add_icon_update_listener(struct wl_listener *listener);

struct theme *theme_manager_get_theme(void);

bool theme_manager_set_widget_theme(const char *name, enum theme_type type);

bool theme_manager_set_font(const char *name, int size);

bool theme_manager_set_accent_color(int32_t color);

bool theme_manager_set_icon_theme(const char *icon_theme_name);

bool theme_manager_set_corner_radius(int32_t radius);

bool theme_manager_set_opacity(int32_t opacity);

struct wlr_buffer *theme_buffer_load(struct theme *theme, float scale, enum theme_buffer_type type,
                                     struct wlr_fbox *src);

const char *theme_icon_get_name(struct icon *icon);

struct wlr_buffer *theme_icon_get_buffer(struct icon *icon, float scale);

struct icon *theme_icon_from_app_id(const char *app_id);

#endif /* _THEME_H_ */
