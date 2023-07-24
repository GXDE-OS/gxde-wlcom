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

    /* general */
    int border_width;
    int padding_height;
    int menu_overlap_x;
    int menu_overlap_y;

    /* others may useful */
    int button_width;
    int corner_radius;
    int title_height;
    int cursor_border;
    int shadow_border;

    /* font */
    const char *font_name;
    int font_size;

    /* window border */
    float window_active_border_color[4];
    float window_inactive_border_color[4];

    /* window titlebar background */
    float window_active_title_bg_color[4];
    float window_inactive_title_bg_color[4];

    /* window titlebar text */
    float window_active_label_text_color[4];
    float window_inactive_label_text_color[4];
    enum justification window_label_text_justify;

    /* button svg string */
    const char *button_svg;

    /* shadow buffer */
    struct wlr_buffer *shadow;

    struct wl_list scaled_buffers;
};

struct theme_manager *theme_manager_create(struct server *server);

void theme_manager_add_update_listener(struct wl_listener *listener);

struct theme *theme_manager_get_current(void);

bool theme_manager_set_theme(const char *name);

struct wlr_buffer *theme_buffer_load(struct theme *theme, float scale, enum theme_buffer_type type,
                                     struct wlr_fbox *src);

#endif /* _THEME_H_ */
