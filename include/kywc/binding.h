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

/**
 * gesture bindings
 */

// TODO: create by gesture bind string
struct gesture_binding *kywc_gesture_binding_create(enum gesture_type type, uint32_t devices,
                                                    uint32_t directions, uint8_t fingers,
                                                    const char *desc);

void kywc_gesture_binding_destroy(struct gesture_binding *binding);

bool kywc_gesture_binding_register(struct gesture_binding *binding,
                                   void (*action)(struct gesture_binding *binding, void *data),
                                   void *data);

/**
 * keysym bindings
 */

struct key_binding *kywc_key_binding_create(const char *keybind, const char *desc);

struct key_binding *kywc_key_binding_create_by_symbol(unsigned int keysym, unsigned int modifiers,
                                                      const char *desc);

void kywc_key_binding_destroy(struct key_binding *binding);

bool kywc_key_binding_register(struct key_binding *binding,
                               void (*action)(struct key_binding *binding, void *data), void *data);

bool kywc_key_binding_update(struct key_binding *binding, unsigned int keysym,
                             unsigned int modifiers, const char *desc);

void kywc_key_binding_unregister(struct key_binding *binding);

bool kywc_key_binding_is_registered(struct key_binding *binding);

#endif /* _KYWC_BINDING_H_ */
