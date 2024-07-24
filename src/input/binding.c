// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>

#include <kywc/binding.h>
#include <kywc/log.h>

#include "input_p.h"
#include "server.h"

struct key_binding {
    struct wl_list link;

    uint32_t modifiers;
    uint32_t keysym;
    bool no_repeat;

    char *keybind;
    char *desc;

    void (*action)(struct key_binding *binding, void *data);
    void *data;
};

struct gesture_binding {
    struct wl_list link;

    enum gesture_type type;
    uint8_t fingers;
    uint32_t devices;
    uint32_t directions;
    uint32_t edges;
    char *desc;

    void (*action)(struct gesture_binding *binding, void *data);
    void *data;
};

static struct bindings {
    struct wl_list keysym_bindings;
    struct wl_list gesture_bindings;
    struct wl_listener server_destroy;
} *bindings = NULL;

static char **split_string(const char *str, const char *delims, size_t *len)
{
    char **split_str = malloc(sizeof(void *) * 10);
    char *copy = strdup(str);
    size_t length = 0;

    char *token = strtok(copy, delims);
    while (token) {
        split_str[length++] = strdup(token);
        token = strtok(NULL, delims);
    }
    free(copy);

    *len = length;
    return split_str;
}

static void free_split_string(char ***item, int count)
{
    if (item == NULL || *item == NULL) {
        return;
    }

    for (int i = 0; i < count; i++) {
        free((*item)[i]);
    }

    free(*item);
}

struct key_binding *kywc_key_binding_create(const char *keybind, const char *desc)
{
    struct key_binding *binding = calloc(1, sizeof(struct key_binding));
    if (!binding) {
        return NULL;
    }

    /* check no_repeat first */
    size_t length = 0;
    char **bind_str = split_string(keybind, ":", &length);
    binding->no_repeat = length == 2 && strcmp(bind_str[1], "no") == 0;

    size_t len = 0;
    char **split_str = split_string(length == 2 ? bind_str[0] : keybind, "+", &len);
    free_split_string(&bind_str, length);

    for (size_t i = 0; i < len; i++) {
        kywc_log(KYWC_DEBUG, "keybind split_str = %s", split_str[i]);
        uint32_t mod = keyboard_get_modifier_mask_by_name(split_str[i]);
        if (mod) {
            binding->modifiers |= mod;
            continue;
        }

        /* whatever keep the lower keysym */
        xkb_keysym_t sym = xkb_keysym_from_name(split_str[i], XKB_KEYSYM_CASE_INSENSITIVE);
        binding->keysym = xkb_keysym_to_lower(sym);
    }

    free_split_string(&split_str, len);

    if (desc) {
        binding->desc = strdup(desc);
    }
    binding->keybind = strdup(keybind);
    wl_list_init(&binding->link);

    return binding;
}

struct key_binding *kywc_key_binding_create_by_symbol(unsigned int keysym, unsigned int modifiers,
                                                      bool no_repeat, const char *desc)
{
    struct key_binding *binding = calloc(1, sizeof(struct key_binding));
    if (!binding) {
        return NULL;
    }

    binding->modifiers = modifiers;
    binding->keysym = keysym;
    binding->no_repeat = no_repeat;

    if (desc) {
        binding->desc = strdup(desc);
    }
    wl_list_init(&binding->link);

    return binding;
}

void kywc_key_binding_destroy(struct key_binding *binding)
{
    wl_list_remove(&binding->link);

    free(binding->keybind);
    free(binding->desc);
    free(binding);
}

static bool key_binding_is_valid(struct key_binding *binding, uint32_t keysym, uint32_t modifiers)
{
    struct key_binding *bind;
    wl_list_for_each(bind, &bindings->keysym_bindings, link) {
        /* skip itself */
        if (bind == binding) {
            continue;
        }

        if (!(modifiers ^ bind->modifiers) && keysym == bind->keysym) {
            kywc_log(KYWC_ERROR, "%s(%s) is already registed by %s(%s)", binding->keybind,
                     binding->desc, bind->keybind, bind->desc);
            return false;
        }
    }

    return true;
}

bool kywc_key_binding_register(struct key_binding *binding,
                               void (*action)(struct key_binding *binding, void *data), void *data)
{
    if (kywc_key_binding_is_registered(binding)) {
        return true;
    }

    if (!key_binding_is_valid(binding, binding->keysym, binding->modifiers)) {
        return false;
    }

    wl_list_insert(&bindings->keysym_bindings, &binding->link);

    if (action) {
        binding->action = action;
    }
    if (data) {
        binding->data = data;
    }

    return true;
}

bool kywc_key_binding_update(struct key_binding *binding, unsigned int keysym,
                             unsigned int modifiers, const char *desc)
{
    if (kywc_key_binding_is_registered(binding) &&
        !key_binding_is_valid(binding, keysym, modifiers)) {
        return false;
    }

    binding->keysym = keysym;
    binding->modifiers = modifiers;

    if (desc) {
        free(binding->desc);
        binding->desc = strdup(desc);
    }

    return true;
}

void kywc_key_binding_unregister(struct key_binding *binding)
{
    wl_list_remove(&binding->link);
    wl_list_init(&binding->link);
}

bool kywc_key_binding_is_registered(struct key_binding *binding)
{
    return !wl_list_empty(&binding->link);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&bindings->server_destroy.link);

    struct key_binding *key_binding, *key_binding_tmp;
    wl_list_for_each_safe(key_binding, key_binding_tmp, &bindings->keysym_bindings, link) {
        kywc_key_binding_destroy(key_binding);
    }

    struct gesture_binding *gesture_binding, *gesture_binding_tmp;
    wl_list_for_each_safe(gesture_binding, gesture_binding_tmp, &bindings->gesture_bindings, link) {
        kywc_gesture_binding_destroy(gesture_binding);
    }

    free(bindings);
    bindings = NULL;
}

bool bindings_create(struct input_manager *input_manager)
{
    bindings = calloc(1, sizeof(struct bindings));
    if (!bindings) {
        return false;
    }

    wl_list_init(&bindings->keysym_bindings);
    wl_list_init(&bindings->gesture_bindings);

    bindings->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(input_manager->server, &bindings->server_destroy);
    return true;
}

static bool match_key_binding(struct keyboard_state *keyboard_state, struct key_binding *binding)
{
    for (size_t j = 0; j < keyboard_state->npressed; j++) {
        uint32_t keysym = keyboard_state->pressed_keysyms[j];
        if (binding->keysym == xkb_keysym_to_lower(keysym)) {
            return true;
        }
    }

    return false;
}

bool bindings_handle_key_binding(struct keyboard_state *keyboard_state, bool *repeat)
{
    struct key_binding *binding;
    wl_list_for_each(binding, &bindings->keysym_bindings, link) {
        if ((keyboard_state->only_one_modifier && binding->keysym) ||
            (!keyboard_state->only_one_modifier && !binding->keysym)) {
            continue;
        }

        if (keyboard_state->last_modifiers ^ binding->modifiers) {
            continue;
        }

        if (keyboard_state->npressed < 1 && binding->keysym) {
            continue;
        }

        if (!binding->keysym || match_key_binding(keyboard_state, binding)) {
            kywc_log(KYWC_DEBUG, "start key binding: %s", binding->desc);
            if (binding->action) {
                binding->action(binding, binding->data);
            }
            *repeat = !binding->no_repeat;
            return true;
        }
    }
    return false;
}

void kywc_gesture_binding_destroy(struct gesture_binding *binding)
{
    wl_list_remove(&binding->link);
    free(binding->desc);
    free(binding);
}

static bool gesture_binding_is_valid(struct gesture_binding *binding, enum gesture_type type,
                                     uint32_t devices, uint32_t directions, uint8_t fingers,
                                     uint8_t edges)
{
    struct gesture_binding *bind;
    wl_list_for_each(bind, &bindings->gesture_bindings, link) {
        /* skip itself */
        if (bind == binding) {
            continue;
        }
        if (bind->type == type && (bind->devices & devices) &&
            (GESTURE_DIRECTION_NONE == bind->directions || (bind->directions & directions)) &&
            (GESTURE_EDGE_NONE == bind->edges || (edges & bind->edges)) &&
            bind->fingers == fingers) {
            return false;
        }
    }

    return true;
}

static uint32_t gesture_string_parse_directions(const char *str)
{
    uint32_t directions = GESTURE_DIRECTION_NONE;

    size_t len = 0;
    char **split_str = split_string(str, "+", &len);

    for (size_t i = 0; i < len; i++) {
        if (strcmp(split_str[i], "none") == 0) {
            directions |= GESTURE_DIRECTION_NONE;
        } else if (strcmp(split_str[i], "up") == 0) {
            directions |= GESTURE_DIRECTION_UP;
        } else if (strcmp(split_str[i], "down") == 0) {
            directions |= GESTURE_DIRECTION_DOWN;
        } else if (strcmp(split_str[i], "left") == 0) {
            directions |= GESTURE_DIRECTION_LEFT;
        } else if (strcmp(split_str[i], "right") == 0) {
            directions |= GESTURE_DIRECTION_RIGHT;
        } else if (strcmp(split_str[i], "inward") == 0) {
            directions |= GESTURE_DIRECTION_INWARD;
        } else if (strcmp(split_str[i], "outward") == 0) {
            directions |= GESTURE_DIRECTION_OUTWARD;
        } else if (strcmp(split_str[i], "clockwise") == 0) {
            directions |= GESTURE_DIRECTION_CLOCKWISE;
        } else if (strcmp(split_str[i], "counterclockwise") == 0) {
            directions |= GESTURE_DIRECTION_COUNTERCLOCKWISE;
        } else {
            kywc_log(KYWC_WARN, "expected directions, got %s", str);
        }
    }

    free_split_string(&split_str, len);

    return directions;
}

static uint32_t gesture_string_parse_edges(const char *str)
{
    uint32_t edges = GESTURE_EDGE_NONE;

    size_t len = 0;
    char **split_str = split_string(str, "+", &len);

    for (size_t i = 0; i < len; i++) {
        if (strcmp(split_str[i], "none") == 0) {
            edges |= GESTURE_EDGE_NONE;
        } else if (strcmp(split_str[i], "top") == 0) {
            edges |= GESTURE_EDGE_TOP;
        } else if (strcmp(split_str[i], "bottom") == 0) {
            edges |= GESTURE_EDGE_BOTTOM;
        } else if (strcmp(split_str[i], "left") == 0) {
            edges |= GESTURE_EDGE_LEFT;
        } else if (strcmp(split_str[i], "right") == 0) {
            edges |= GESTURE_EDGE_RIGHT;
        } else {
            kywc_log(KYWC_WARN, "expected edges, got %s", str);
        }
    }

    free_split_string(&split_str, len);

    return edges;
}

struct gesture_binding *kywc_gesture_binding_create_by_string(const char *gestures,
                                                              const char *desc)
{
    enum gesture_type type;
    uint8_t fingers;
    uint32_t devices;
    uint32_t directions;
    uint32_t edges = GESTURE_EDGE_NONE;

    size_t len = 0;
    char **split_str = split_string(gestures, ":", &len);
    if (len != 4 && len != 5) {
        kywc_log(KYWC_ERROR, "expected <gesture>:<device>:<fingers>:<direction>[:edges] got %s",
                 gestures);
        goto err;
    }
    // type
    if (strcmp(split_str[0], "hold") == 0) {
        type = GESTURE_TYPE_HOLD;
    } else if (strcmp(split_str[0], "pinch") == 0) {
        type = GESTURE_TYPE_PINCH;
    } else if (strcmp(split_str[0], "swipe") == 0) {
        type = GESTURE_TYPE_SWIPE;
    } else {
        kywc_log(KYWC_ERROR, "expected hold|pinch|swipe, got %s", gestures);
        goto err;
    }
    // device
    if (strcmp(split_str[1], "any") == 0) {
        devices = GESTURE_DEVICE_TOUCHPAD | GESTURE_DEVICE_TOUCHSCREEN;
    } else if (strcmp(split_str[1], "touch") == 0) {
        devices = GESTURE_DEVICE_TOUCHSCREEN;
    } else if (strcmp(split_str[1], "touchpad") == 0) {
        devices = GESTURE_DEVICE_TOUCHPAD;
    } else {
        kywc_log(KYWC_ERROR, "expected any|touch|touchpad, got %s", gestures);
        goto err;
    }
    /* fingers: 1 - 9 */
    /* direction: up down left right inward outward clockwise counterclockwise */
    /* edge: top bottom left right */
    if ('1' <= split_str[2][0] && split_str[2][0] <= '9') {
        fingers = atoi(split_str[2]);
        directions = gesture_string_parse_directions(split_str[3]);
        if (len > 4) {
            edges = gesture_string_parse_edges(split_str[4]);
        }
    } else {
        kywc_log(KYWC_ERROR, "expected 1 - 9 got %s", gestures);
        goto err;
    }

    free_split_string(&split_str, len);

    kywc_log(KYWC_DEBUG, "gesture binding: %s", gestures);
    return kywc_gesture_binding_create(type, devices, directions, edges, fingers, desc);

err:
    free_split_string(&split_str, len);
    return NULL;
}

static bool gesture_checked(enum gesture_type type, uint32_t devices, uint32_t directions,
                            uint32_t edges, uint8_t fingers)
{
    /* gesture type、devices and fingers cannot be 0 */
    if (type == GESTURE_TYPE_NONE || devices == GESTURE_DEVICE_NONE || fingers == 0) {
        return false;
    }

    /* pinch gesture fingers require no less than 2 */
    if (type == GESTURE_TYPE_PINCH && fingers < 2) {
        return false;
    }

    /* pinch or touchpad all gestures edges need to be 0 */
    if ((type == GESTURE_TYPE_PINCH || devices & GESTURE_DEVICE_TOUCHPAD) &&
        edges != GESTURE_EDGE_NONE) {
        return false;
    }

    /* the number of fingers for touchpad swipe gestures need to be greater than 1 */
    if (type == GESTURE_TYPE_SWIPE && devices & GESTURE_DEVICE_TOUCHPAD && fingers == 1) {
        return false;
    }

    /* the edge of the swipe gesture for one finger of the touchscreen cannot be 0 */
    if (type == GESTURE_TYPE_SWIPE && edges == GESTURE_EDGE_NONE && fingers == 1) {
        return false;
    }

    /* swipe gesture from edge, the start edges and directions need to be set properly */
    if (edges != GESTURE_EDGE_NONE &&
        ((directions & GESTURE_DIRECTION_LEFT && !(edges & GESTURE_EDGE_RIGHT)) ||
         (directions & GESTURE_DIRECTION_RIGHT && !(edges & GESTURE_EDGE_LEFT)) ||
         (directions & GESTURE_DIRECTION_UP && !(edges & GESTURE_EDGE_BOTTOM)) ||
         (directions & GESTURE_DIRECTION_DOWN && !(edges & GESTURE_EDGE_TOP)))) {
        return false;
    }

    return true;
}

struct gesture_binding *kywc_gesture_binding_create(enum gesture_type type, uint32_t devices,
                                                    uint32_t directions, uint32_t edges,
                                                    uint8_t fingers, const char *desc)
{
    if (!gesture_checked(type, devices, directions, edges, fingers)) {
        kywc_log(KYWC_ERROR, "gesture checkes are illega");
        return NULL;
    }

    struct gesture_binding *binding = calloc(1, sizeof(struct gesture_binding));
    if (!binding) {
        return NULL;
    }

    binding->type = type;
    binding->devices = devices;
    binding->directions = directions;
    binding->fingers = fingers;
    binding->edges = edges;
    if (desc) {
        binding->desc = strdup(desc);
    }
    wl_list_init(&binding->link);

    return binding;
}

bool kywc_gesture_binding_register(struct gesture_binding *binding,
                                   void (*action)(struct gesture_binding *binding, void *data),
                                   void *data)
{
    if (!wl_list_empty(&binding->link)) {
        return true;
    }
    if (!gesture_binding_is_valid(binding, binding->type, binding->devices, binding->directions,
                                  binding->fingers, binding->edges)) {
        return false;
    }

    wl_list_insert(&bindings->gesture_bindings, &binding->link);
    binding->action = action;
    binding->data = data;

    return true;
}

bool bindings_handle_gesture_binding(struct gesture_state *gesture_state)
{
    struct gesture_binding *binding;
    wl_list_for_each(binding, &bindings->gesture_bindings, link) {
        if (gesture_state->type != binding->type) {
            continue;
        }
        if (gesture_state->fingers != binding->fingers) {
            continue;
        }
        if ((gesture_state->device & binding->devices) &&
            (binding->directions == GESTURE_DIRECTION_NONE ||
             gesture_state->directions & binding->directions) &&
            (binding->edges == GESTURE_EDGE_NONE || gesture_state->edge & binding->edges)) {
            kywc_log(KYWC_DEBUG, "start gesture binding: %s", binding->desc);
            if (binding->action) {
                binding->action(binding, binding->data);
            }
            return true;
        }
    }
    return false;
}
