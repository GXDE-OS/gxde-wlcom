// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _KYWC_BINDING_H_
#define _KYWC_BINDING_H_

#include <stdbool.h>
#include <stdint.h>

enum gesture_type {
    GESTURE_TYPE_NONE = 0,
    GESTURE_TYPE_PINCH,
    GESTURE_TYPE_SWIPE,
    GESTURE_TYPE_HOLD,
};

enum gesture_device {
    GESTURE_DEVICE_NONE = 0,
    GESTURE_DEVICE_TOUCHPAD = 1 << 0,
    GESTURE_DEVICE_TOUCHSCREEN = 1 << 1,
};

enum gesture_direction {
    GESTURE_DIRECTION_NONE = 0,
    // Directions based on delta x and y
    GESTURE_DIRECTION_UP = 1 << 0,
    GESTURE_DIRECTION_DOWN = 1 << 1,
    GESTURE_DIRECTION_LEFT = 1 << 2,
    GESTURE_DIRECTION_RIGHT = 1 << 3,
    // Directions based on scale
    GESTURE_DIRECTION_INWARD = 1 << 4,
    GESTURE_DIRECTION_OUTWARD = 1 << 5,
    // Directions based on rotation
    GESTURE_DIRECTION_CLOCKWISE = 1 << 6,
    GESTURE_DIRECTION_COUNTERCLOCKWISE = 1 << 7,
};

enum gesture_edge {
    GESTURE_EDGE_NONE = 0,
    GESTURE_EDGE_TOP = 1 << 0,
    GESTURE_EDGE_BOTTOM = 1 << 1,
    GESTURE_EDGE_LEFT = 1 << 2,
    GESTURE_EDGE_RIGHT = 1 << 3,
};

/* key binding type */
enum key_binding_type {
    KEY_BINDING_TYPE_CUSTOM_DEF = 0,
    KEY_BINDING_TYPE_WIN_MENU,
    KEY_BINDING_TYPE_SWITCH_WORKSPACE,
    KEY_BINDING_TYPE_WINDOW_ACTION,
    KEY_BINDING_TYPE_MAXIMIZED_VIEWS,
    KEY_BINDING_TYPE_CTRL_VIEWS,
    KEY_BINDING_TYPE_NUM,
};

/**
 * gesture bindings
 */

struct gesture_binding *kywc_gesture_binding_create_by_string(const char *gestures,
                                                              const char *desc);

struct gesture_binding *kywc_gesture_binding_create(enum gesture_type type, uint32_t devices,
                                                    uint32_t directions, uint32_t edges,
                                                    uint8_t fingers, const char *desc);

void kywc_gesture_binding_destroy(struct gesture_binding *binding);

bool kywc_gesture_binding_register(struct gesture_binding *binding,
                                   void (*action)(struct gesture_binding *binding, void *data),
                                   void *data);

/**
 * keysym bindings
 */

struct key_binding *kywc_key_binding_create(const char *keybind, const char *desc);

struct key_binding *kywc_key_binding_create_by_symbol(unsigned int keysym, unsigned int modifiers,
                                                      bool no_repeat, const char *desc);

void kywc_key_binding_destroy(struct key_binding *binding);

bool kywc_key_binding_register(struct key_binding *binding, enum key_binding_type type,
                               void (*action)(struct key_binding *binding, void *data), void *data);

bool kywc_key_binding_update(struct key_binding *binding, unsigned int keysym,
                             unsigned int modifiers, const char *desc);

void kywc_key_binding_unregister(struct key_binding *binding);

bool kywc_key_binding_is_registered(struct key_binding *binding);

typedef bool (*binding_iterator_func_t)(struct key_binding *binding, char *keybind, char *desc,
                                        int32_t modifiers, int32_t key);

void kywc_key_binding_for_each(binding_iterator_func_t iterator);

void kywc_key_binding_block_all(bool block);

void kywc_key_binding_block_type(enum key_binding_type type, bool block);

enum key_binding_type kywc_key_binding_type_by_name(const char *name);

#endif /* _KYWC_BINDING_H_ */
