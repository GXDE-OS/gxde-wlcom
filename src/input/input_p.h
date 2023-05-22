#ifndef _INPUT_P_H_
#define _INPUT_P_H_

#include "input/cursor.h"

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

#endif /* _INPUT_P_H_ */
