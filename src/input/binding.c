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

    char *keybind;
    char *desc;

    void (*action)(struct key_binding *binding, void *data);
    void *data;
};

struct gesture_binding {
    struct wl_list link;

    enum gesture_type type;
    uint8_t fingers;
    uint32_t directions;
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
    size_t lenth = 0;

    char *token = strtok(copy, delims);
    while (token) {
        split_str[lenth++] = strdup(token);
        token = strtok(NULL, delims);
    }
    free(copy);

    *len = lenth;
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

    size_t len = 0;
    char **split_str = split_string(keybind, "+", &len);

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
                                                      const char *desc)
{
    struct key_binding *binding = calloc(1, sizeof(struct key_binding));
    if (!binding) {
        return NULL;
    }

    binding->modifiers = modifiers;
    binding->keysym = keysym;

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
    binding->action = action;
    binding->data = data;

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

bool bindings_handle_key_binding(struct keyboard_state *keyboard_state)
{
    struct key_binding *binding;
    wl_list_for_each(binding, &bindings->keysym_bindings, link) {
        if (keyboard_state->last_modifiers ^ binding->modifiers) {
            continue;
        }
        if (keyboard_state->npressed < 1) {
            continue;
        }

        if (match_key_binding(keyboard_state, binding)) {
            kywc_log(KYWC_DEBUG, "start key binding: %s", binding->desc);
            if (binding->action) {
                binding->action(binding, binding->data);
            }
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
                                     uint8_t fingers, uint32_t directions)
{
    struct gesture_binding *bind;
    wl_list_for_each(bind, &bindings->gesture_bindings, link) {
        /* skip itself */
        if (bind == binding) {
            continue;
        }

        if (bind->type == type && (bind->directions & directions) && bind->fingers == fingers) {
            return false;
        }
    }

    return true;
}

struct gesture_binding *kywc_gesture_binding_create(enum gesture_type type,
                                                    enum gesture_direction directions,
                                                    uint8_t fingers, const char *desc)
{
    // TODO: check type, directions and fingers

    struct gesture_binding *binding = calloc(1, sizeof(struct gesture_binding));
    if (!binding) {
        return NULL;
    }

    binding->type = type;
    binding->directions = directions;
    binding->fingers = fingers;
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
    if (!gesture_binding_is_valid(binding, binding->type, binding->directions, binding->fingers)) {
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
        if (gesture_state->directions & binding->directions) {
            kywc_log(KYWC_DEBUG, "start gesture binding: %s", binding->desc);
            if (binding->action) {
                binding->action(binding, binding->data);
            }
            return true;
        }
    }
    return false;
}
