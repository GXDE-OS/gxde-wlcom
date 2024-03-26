// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _KEYBOARD_H_
#define _KEYBOARD_H_

#include "input.h"

#define MAX_PRESSED_KEY 10

struct keyboard_state {
    uint32_t pressed_keysyms[MAX_PRESSED_KEY];
    uint32_t last_keysym;
    uint32_t last_modifiers;
    size_t npressed;
    bool only_one_modifier;
};

struct keyboard {
    struct wlr_keyboard *wlr_keyboard;

    struct wl_list link;
    struct seat *seat;

    struct wl_listener key;
    struct wl_listener modifiers;

    bool is_virtual;
    struct keyboard_state state;

    struct {
        uint32_t key;
        struct wl_event_source *timer;
    } repeat;
};

struct keyboard *keyboard_create(struct seat *seat, struct wlr_keyboard *wlr_keyboard);

void keyboard_destroy(struct keyboard *keyboard);

void keyboard_add_input(struct seat *seat, struct input *input);

void keyboard_remove_input(struct input *input);

uint32_t keyboard_get_modifier_mask_by_name(const char *name);

const char *keyboard_get_modifier_name_by_mask(uint32_t modifier);

void keyboard_send_key(struct keyboard *keyboard, uint32_t key, bool pressed);

uint32_t keyboard_get_locks(struct keyboard *keyboard);

bool keyboard_has_no_input(struct keyboard *keyboard);

#endif /* _KEYBOARD_H_ */
