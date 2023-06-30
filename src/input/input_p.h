#ifndef _INPUT_P_H_
#define _INPUT_P_H_

#include "input/cursor.h"
#include "input/keyboard.h"

struct input_manager {
    struct server *server;

    struct wl_list seats;
    struct wl_list inputs;

    struct {
        struct wl_signal new_input;
        struct wl_signal new_seat;
    } events;

    struct config *config;
    struct bindings *bindings;

    struct wl_listener new_input;
    struct wl_listener new_virtual_pointer;
    struct wl_listener new_virtual_keyboard;
    struct wl_listener server_destroy;
};

bool input_manager_config_init(struct input_manager *input_manager);

bool input_read_config(struct input *input, struct input_state *state);

void input_write_config(struct input *input);

void input_prop_and_state_debug(struct input *input);

/**
 * libinput helper functions
 */
void libinput_get_prop(struct input *input, struct input_prop *prop);

void libinput_get_state(struct input *input, struct input_state *state);

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
 * input method and text input
 */

bool input_method_manager_create(struct input_manager *input_manager);

void input_method_set_focus(struct seat *seat, struct wlr_surface *wlr_surface);

bool input_method_handle_key(struct keyboard *keyboard, uint32_t time, uint32_t key,
                             uint32_t state);

bool input_method_handle_modifiers(struct keyboard *keyboard);

#endif /* _INPUT_P_H_ */
