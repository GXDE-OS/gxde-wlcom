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
    int resize_border;
    int shadow_border;

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

    /* selected color */
    float selected_color[4];
    float accent_color[4];

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

bool theme_manager_set_font(const char *name, int size);

struct wlr_buffer *theme_buffer_load(struct theme *theme, float scale, enum theme_buffer_type type,
                                     struct wlr_fbox *src);

#endif /* _THEME_H_ */
