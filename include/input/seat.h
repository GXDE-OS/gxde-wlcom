// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _SEAT_H_
#define _SEAT_H_

#include "input.h"
#include "scene/scene.h"

struct seat_pointer_grab;
struct seat_keyboard_grab;
struct seat_touch_grab;

struct seat_pointer_grab_interface {
    bool (*motion)(struct seat_pointer_grab *grab, uint32_t time, double lx, double ly);
    bool (*button)(struct seat_pointer_grab *grab, uint32_t time, uint32_t button, bool pressed);
    bool (*axis)(struct seat_pointer_grab *grab, uint32_t time, bool vertical, double value);
    void (*cancel)(struct seat_pointer_grab *grab);
};

struct seat_pointer_grab {
    const struct seat_pointer_grab_interface *interface;
    struct seat *seat;
    void *data;
};

struct seat_keyboard_grab_interface {
    bool (*key)(struct seat_keyboard_grab *grab, uint32_t time, uint32_t key, bool pressed,
                uint32_t modidiers);
    void (*cancel)(struct seat_keyboard_grab *grab);
};

struct seat_keyboard_grab {
    const struct seat_keyboard_grab_interface *interface;
    struct seat *seat;
    void *data;
};

struct seat_touch_grab_interface {
    bool (*touch)(struct seat_touch_grab *grab, uint32_t time, bool down);
    bool (*motion)(struct seat_touch_grab *grab, uint32_t time, double lx, double ly);
    void (*cancel)(struct seat_touch_grab *grab);
};

struct seat_touch_grab {
    const struct seat_touch_grab_interface *interface;
    struct seat *seat;
    void *data;
};

struct seat {
    struct wlr_seat *wlr_seat;
    char *name;
    struct wl_list link;

    uint32_t caps; // enum wl_seat_capability

    struct input_manager *manager;

    /* input devices attached */
    struct wl_list inputs;

    // TODO: timer to hide cursor
    struct cursor *cursor;
    struct keyboard *keyboard;
    struct wl_list keyboards;

    /* internal grabs */
    struct seat_pointer_grab *pointer_grab;
    struct seat_keyboard_grab *keyboard_grab;
    struct seat_touch_grab *touch_grab;

    struct ky_scene *scene;
    struct wlr_output_layout *layout;
    struct wlr_pointer_gestures_v1 *pointer_gestures;

    struct input_method_relay *relay;
    struct selection *selection;

    struct {
        struct wl_signal destroy;
    } events;

    struct {
        const char *cursor_theme;
        uint32_t cursor_size;
        uint32_t keyboard_lock_mode;
        uint32_t keyboard_lock;
    } state;

    struct wl_listener server_start;
    struct wl_listener destroy;
};

struct seat *seat_create(struct input_manager *input_manager, const char *name);

void seat_destroy(struct seat *seat);
void seat_consider_destroy(struct seat *seat);

void seat_add_input(struct seat *seat, struct input *input);

void seat_remove_input(struct input *input);

void seat_reset_input_gesture(struct seat *seat);

struct seat *seat_from_resource(struct wl_resource *resource);

struct seat *seat_from_wlr_seat(struct wlr_seat *wlr_seat);

void seat_set_cursor(struct seat *seat, const char *cursor_theme, uint32_t cursor_size);

void seat_start_pointer_grab(struct seat *seat, struct seat_pointer_grab *pointer_grab);
void seat_end_pointer_grab(struct seat *seat, struct seat_pointer_grab *pointer_grab);

void seat_start_keyboard_grab(struct seat *seat, struct seat_keyboard_grab *keyboard_grab);
void seat_end_keyboard_grab(struct seat *seat, struct seat_keyboard_grab *keyboard_grab);

void seat_start_touch_grab(struct seat *seat, struct seat_touch_grab *touch_grab);
void seat_end_touch_grab(struct seat *seat, struct seat_touch_grab *touch_grab);

struct wlr_surface;
void seat_notify_motion(struct seat *seat, struct wlr_surface *surface, uint32_t time, double sx,
                        double sy, bool first_enter);
void seat_notify_button(struct seat *seat, uint32_t time, uint32_t button, bool pressed);
void seat_notify_leave(struct seat *seat, struct wlr_surface *surface);

void seat_focus_surface(struct seat *seat, struct wlr_surface *surface);

void seat_feed_pointer_motion(struct seat *seat, double x, double y, bool absolute);
void seat_feed_pointer_button(struct seat *seat, uint32_t button, bool pressed);
void seat_feed_pointer_axis(struct seat *seat, uint32_t axis, double step);
void seat_feed_keyboard_key(struct seat *seat, uint32_t key, bool pressed);

#endif /* _SEAT_H_ */
