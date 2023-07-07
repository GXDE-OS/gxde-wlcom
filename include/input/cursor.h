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

struct cursor_node {
    /* node at the cursor position */
    struct ky_scene_node *node;
    /* do something if node destroy */
    struct wl_listener destroy;
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

    struct wl_listener touch_up;
    struct wl_listener touch_down;
    struct wl_listener touch_motion;
    struct wl_listener touch_cancel;
    struct wl_listener touch_frame;

    struct wl_listener tablet_tool_axis;
    struct wl_listener tablet_tool_proximity;
    struct wl_listener tablet_tool_tip;
    struct wl_listener tablet_tool_button;

    struct wl_listener request_set_cursor;

    bool touch_simulation_pointer;
    bool pointer_touch_up;
    int32_t pointer_touch_id;

    bool tablet_tool_tip_simulation_pointer;
    bool tablet_tool_button_simulation_pointer;

    enum cursor_name name;
    float scale;
    /* current cursor position in layout coord */
    double lx, ly;
    bool client_requested;

    /* cached button clicked info */
    uint32_t last_click_time;
    uint32_t last_click_button;
    bool last_click_pressed;

    /* node below the cursor */
    struct cursor_node hover;
    /* node if button clicked */
    struct cursor_node focus;
    /* current curosr position in node coord */
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

void cursor_move(struct cursor *cursor, struct wlr_input_device *dev, double x, double y,
                 bool delta, bool absolute);

#endif /* _CURSOR_H_ */
