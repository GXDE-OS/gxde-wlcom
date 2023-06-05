#ifndef _CURSOR_H_
#define _CURSOR_H_

#include "event.h"
#include "input.h"

enum cursor_name {
    CURSOR_NONE = 0,
    CURSOR_DEFAULT,
    CURSOR_MOVE,
    CURSOR_RESIZE_TOP_LEFT,
    CURSOR_RESIZE_TOP,
    CURSOR_RESIZE_TOP_RIGHT,
    CURSOR_RESIZE_RIGHT,
    CURSOR_RESIZE_BOTTOM_RIGHT,
    CURSOR_RESIZE_BOTTOM,
    CURSOR_RESIZE_BOTTOM_LEFT,
    CURSOR_RESIZE_LEFT,
};

struct cursor {
    struct wlr_cursor *wlr_cursor;
    struct wlr_xcursor_manager *xcursor_manager;
    struct seat *seat;

    struct wl_listener motion;
    struct wl_listener motion_absolute;
    struct wl_listener button;
    struct wl_listener axis;
    struct wl_listener frame;

    struct wl_listener swipe_begin;
    struct wl_listener swipe_update;
    struct wl_listener swipe_end;
    struct wl_listener pinch_begin;
    struct wl_listener pinch_update;
    struct wl_listener pinch_end;
    struct wl_listener hold_begin;
    struct wl_listener hold_end;

    struct wl_signal touch_up;
    struct wl_signal touch_down;
    struct wl_signal touch_motion;
    struct wl_signal touch_cancel;
    struct wl_signal touch_frame;

    struct wl_listener tool_axis;
    struct wl_listener tool_proximity;
    struct wl_listener tool_tip;
    struct wl_listener tool_button;
    bool tool_tip_simulation_pointer;
    bool tool_button_simulation_pointer;

    struct wl_listener request_set_cursor;
    bool client_requested;

    enum cursor_name name;
    float scale;
    /* current cursor position in layout coord */
    double lx, ly;

    /* cached button clicked info */
    uint32_t last_click_time;
    uint32_t last_click_button;
    bool last_click_pressed;

    /* node below the cursor */
    struct ky_scene_node *hover;
    /* do something if hover node destroy */
    struct wl_listener hover_destroy;
    /* node if button clicked */
    struct ky_scene_node *focus;
    /* do something if focus node destroy */
    struct wl_listener focus_destroy;
    /* current hover position in node coord */
    double sx, sy;

    /* special: hold a pressed button and leave surface */
    bool hold_mode;
};

struct cursor *cursor_create(struct seat *seat);

void curosr_add_input(struct seat *seat, struct input *input);

void cursor_remove_input(struct input *input);

void cursor_destroy(struct cursor *cursor);

void cursor_set_image(struct cursor *cursor, enum cursor_name name);

void cursor_reload_image(struct cursor *cursor, float scale);

void cursor_move(struct cursor *cursor, double x, double y, bool delta);

#endif /* _CURSOR_H_ */
