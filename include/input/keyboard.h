#ifndef _KEYBOARD_H_
#define _KEYBOARD_H_

#include "input.h"

#define MAX_PRESSED_KEY 10

struct keyboard_state {
    uint32_t pressed_keysyms[MAX_PRESSED_KEY];
    uint32_t last_keysym;
    uint32_t last_modifiers;
    size_t npressed;
};

struct keyboard {
    struct wlr_keyboard *wlr_keyboard;

    struct wl_list link;
    struct seat *seat;

    struct wl_listener key;
    struct wl_listener modifiers;

    bool is_virtual;
    struct keyboard_state state;
};

void keyboard_destroy(struct keyboard *keyboard);

void keyboard_add_input(struct seat *seat, struct input *input);

void keyboard_remove_input(struct input *input);

uint32_t keyboard_get_modifier_mask_by_name(const char *name);

const char *keyboard_get_modifier_name_by_mask(uint32_t modifier);

/**
 * bindings
 */

struct bindings *bindings_create(struct input_manager *input_manager);

void bindings_destroy(struct bindings *bindings);

bool bindings_handle_key_binding(struct keyboard_state *keyboard_state);

#endif /* _KEYBOARD_H_ */
