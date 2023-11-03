// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _INPUT_P_H_
#define _INPUT_P_H_

#include "input/cursor.h"
#include "input/gesture.h"
#include "input/keyboard.h"

struct input_manager {
    struct server *server;

    struct wl_list seats;
    struct wl_list inputs;

    struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard;
    struct wlr_virtual_pointer_manager_v1 *virtual_pointer;
    struct wlr_pointer_gestures_v1 *pointer_gestures;

    struct {
        struct wl_signal new_input;
        struct wl_signal new_seat;
    } events;

    struct config *config;
    struct config *seat_config;

    struct wl_listener new_input;
    struct wl_listener new_virtual_pointer;
    struct wl_listener new_virtual_keyboard;
    struct wl_listener server_destroy;
};

bool input_manager_config_init(struct input_manager *input_manager);

bool input_read_config(struct input *input, struct input_state *state);

void input_write_config(struct input *input);

void input_prop_and_state_debug(struct input *input);

void input_manager_switch_vt(unsigned vt);

void input_notify_destroy(struct input *input);

void input_notify_create(struct input *input);

struct seat *seat_by_name(const char *seat_name);

void cursor_set_xcursor_manager(struct cursor *cursor, const char *theme, uint32_t size,
                                bool saved);

bool seat_read_config(struct seat *seat);

void seat_write_config(struct seat *seat);

/**
 * libinput helper functions
 */
void libinput_get_prop(struct input *input, struct input_prop *prop);

void libinput_get_state(struct input *input, struct input_state *state);

void libinput_get_default_state(struct input *input, struct input_state *state);

bool libinput_set_state(struct input *input, struct input_state *state);

/**
 * monitor for input cursor and others
 */
struct input_monitor *input_monitor_create(struct input_manager *input_manager);

void cursor_move_to_output_center(struct cursor *cursor, struct kywc_output *kywc_output);

/**
 * idle manager
 */

bool idle_manager_create(struct server *server);

void idle_manager_set_inhibited(bool inhibited);

void idle_manager_notify_activity(struct seat *seat);

/* destroy_func can be NULL */
struct idle *idle_manager_add_idle(struct seat *seat, bool support_inhibit, uint32_t timeout,
                                   void (*idle_func)(struct idle *idle, void *data),
                                   void (*resume_func)(struct idle *idle, void *data),
                                   void (*destroy_func)(struct idle *idle, void *data), void *data);

void idle_destroy(struct idle *idle);

/**
 * idle inhibitor manager
 */

bool idle_inhibit_manager_create(struct server *server);

/**
 * input method and text input
 */

bool input_method_manager_create(struct input_manager *input_manager);

void input_method_set_focus(struct seat *seat, struct wlr_surface *wlr_surface);

bool input_method_handle_key(struct keyboard *keyboard, uint32_t time, uint32_t key,
                             uint32_t state);

bool input_method_handle_modifiers(struct keyboard *keyboard);

/**
 * selection drag icon
 */

bool selection_manager_create(struct input_manager *input_manager);

void selection_handle_cursor_move(struct seat *seat, int lx, int ly);

bool selection_is_draging(struct seat *seat);

/**
 * tablet manager
 */

struct wlr_tablet_tool_axis_event;
struct wlr_tablet_tool_proximity_event;
struct wlr_tablet_tool_tip_event;
struct wlr_tablet_tool_button_event;

bool tablet_manager_create(struct input_manager *input_manager);

void tablet_set_focus(struct seat *seat, struct wlr_surface *surface);

void tablet_handle_tool_axis(struct wlr_tablet_tool_axis_event *event);

bool tablet_handle_tool_proximity(struct wlr_tablet_tool_proximity_event *event);

bool tablet_handle_tool_tip(struct wlr_tablet_tool_tip_event *event);

bool tablet_handle_tool_button(struct wlr_tablet_tool_button_event *event);

/**
 * touchscreen manager
 */

struct wlr_touch_up_event;
struct wlr_touch_down_event;
struct wlr_touch_motion_event;
struct wlr_touch_cancel_event;

bool touch_manager_create(struct input_manager *input_manager);

bool touch_handle_down(struct wlr_touch_down_event *event);

void touch_handle_up(struct wlr_touch_up_event *event, bool handle);

void touch_handle_motion(struct wlr_touch_motion_event *event, bool handle);

void touch_handle_cancel(struct wlr_touch_cancel_event *event, bool handle);

/**
 * binding manager for keysym, gesture
 */

bool bindings_create(struct input_manager *input_manager);

bool bindings_handle_key_binding(struct keyboard_state *keyboard_state, bool *repeat);

bool bindings_handle_gesture_binding(struct gesture_state *gesture_state);

/**
 * seat pointer and keyboard feed event
 */

void cursor_feed_motion(struct cursor *cursor, uint32_t time);

void cursor_feed_button(struct cursor *cursor, uint32_t button, bool pressed, uint32_t time);

void cursor_feed_axis(struct cursor *cursor, uint32_t orientation, uint32_t source, double delta,
                      int32_t delta_discrete, uint32_t time);

void keyboard_feed_key(struct keyboard *keyboard, uint32_t key, bool pressed, uint32_t time,
                       uint32_t modifiers);

/**
 * input action for keyshort, gesture binding
 */

bool input_action_manager_create(struct input_manager *input_manager);

#endif /* _INPUT_P_H_ */
