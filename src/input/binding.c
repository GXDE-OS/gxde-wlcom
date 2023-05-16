#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>

#include <kywc/binding.h>
#include <kywc/log.h>

#include "input.h"

struct key_binding {
    struct wl_list link;

    uint32_t modifiers;
    size_t keysyms_len;
    uint32_t *keysyms;

    char *keybind;
    char *desc;

    void (*action)(struct key_binding *binding, void *data);
    void *data;
};

static struct bindings {
    struct input_manager *manager;
    struct wl_list keysym_bindings;
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

    xkb_keysym_t keysyms[MAX_PRESSED_KEY];
    uint32_t current_modifiers = 0;
    size_t lenth = 0;

    char **split_str = split_string(keybind, "+", &lenth);
    for (size_t i = 0; i < lenth; i++) {
        kywc_log(KYWC_DEBUG, "keybind split_str = %s", split_str[i]);
        uint32_t mod = keyboard_get_modifier_mask_by_name(split_str[i]);
        if (mod) {
            binding->modifiers |= mod;
            current_modifiers = mod;
            continue;
        }
        xkb_keysym_t sym =
            xkb_keysym_to_lower(xkb_keysym_from_name(split_str[i], XKB_KEYSYM_CASE_INSENSITIVE));
        keysyms[binding->keysyms_len++] =
            !(current_modifiers & (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CAPS)) ? sym : sym - 32;
    }
    free_split_string(&split_str, lenth);

    binding->keysyms = calloc(1, sizeof(xkb_keysym_t) * binding->keysyms_len);
    memcpy(binding->keysyms, keysyms, binding->keysyms_len * sizeof(xkb_keysym_t));
    if (desc) {
        binding->desc = strdup(desc);
    }
    binding->keybind = strdup(keybind);
    wl_list_init(&binding->link);

    return binding;
}

void kywc_key_binding_destroy(struct key_binding *binding)
{
    wl_list_remove(&binding->link);

    free(binding->keysyms);
    free(binding->keybind);
    free(binding->desc);
    free(binding);
}

bool kywc_key_binding_register(struct key_binding *binding,
                               void (*action)(struct key_binding *binding, void *data), void *data)
{
    struct key_binding *bind;
    bool match = false;

    // TODO: opt with keybind string ?
    wl_list_for_each(bind, &bindings->keysym_bindings, link) {
        if (!(binding->modifiers ^ bind->modifiers)) {
            match = true;
            size_t syms_len = bind->keysyms_len >= binding->keysyms_len ? binding->keysyms_len
                                                                        : bind->keysyms_len;
            for (size_t i = 0; i < syms_len; ++i) {
                uint32_t keysyms = bind->keysyms[i];
                if (binding->keysyms[i] != keysyms) {
                    match = false;
                    break;
                }
            }
        }
        if (match) {
            kywc_log(KYWC_ERROR, "%s is already registed by %s(%s)", binding->keybind,
                     bind->keybind, bind->desc);
            return false;
        }
    }

    wl_list_insert(&bindings->keysym_bindings, &binding->link);
    binding->action = action;
    binding->data = data;

    return true;
}

struct bindings *bindings_create(struct input_manager *input_manager)
{
    bindings = calloc(1, sizeof(struct bindings));
    if (!bindings) {
        return NULL;
    }

    wl_list_init(&bindings->keysym_bindings);

    return bindings;
}

void bindings_destroy(struct bindings *bindings)
{
    struct key_binding *bind, *tmp;
    wl_list_for_each_safe(bind, tmp, &bindings->keysym_bindings, link) {
        kywc_key_binding_destroy(bind);
    }

    free(bindings);
    bindings = NULL;
}

static bool match_key_binding(struct keyboard_state *keyboard_state, struct key_binding *binding)
{
    bool match = false;
    for (size_t i = 0; i < binding->keysyms_len; i++) {
        match = false;
        for (size_t j = 0; j < keyboard_state->npressed; j++) {
            if (binding->keysyms[i] == keyboard_state->pressed_keysyms[j]) {
                match = true;
                break;
            }
        }
        if (!match) {
            break;
        }
    }
    return match;
}

bool bindings_handle_key_binding(struct keyboard_state *keyboard_state)
{
    struct key_binding *binding;
    wl_list_for_each(binding, &bindings->keysym_bindings, link) {
        if (keyboard_state->last_modifiers ^ binding->modifiers) {
            continue;
        }
        if (keyboard_state->npressed < binding->keysyms_len) {
            continue;
        }

        if (match_key_binding(keyboard_state, binding)) {
            kywc_log(KYWC_DEBUG, "start binding: %s", binding->desc);
            if (binding->action) {
                binding->action(binding, binding->data);
            }
            return true;
        }
    }
    return false;
}
