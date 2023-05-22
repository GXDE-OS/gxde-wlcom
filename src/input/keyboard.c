#include <stdlib.h>
#include <strings.h>
#include <xkbcommon/xkbcommon.h>

#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_seat.h>

#include <kywc/log.h>

#include "input/keyboard.h"
#include "input/seat.h"

static struct modifier {
    char *name;
    uint32_t mod;
} modifiers[] = {
    { XKB_MOD_NAME_SHIFT, WLR_MODIFIER_SHIFT },
    { XKB_MOD_NAME_CAPS, WLR_MODIFIER_CAPS },
    { "Ctrl", WLR_MODIFIER_CTRL },
    { XKB_MOD_NAME_CTRL, WLR_MODIFIER_CTRL },
    { "Alt", WLR_MODIFIER_ALT },
    { XKB_MOD_NAME_ALT, WLR_MODIFIER_ALT },
    { XKB_MOD_NAME_NUM, WLR_MODIFIER_MOD2 },
    { "Mod3", WLR_MODIFIER_MOD3 },
    { "Win", WLR_MODIFIER_LOGO },
    { XKB_MOD_NAME_LOGO, WLR_MODIFIER_LOGO },
    { "Mod5", WLR_MODIFIER_MOD5 },
};

uint32_t keyboard_get_modifier_mask_by_name(const char *name)
{
    for (int i = 0; i < (int)(sizeof(modifiers) / sizeof(struct modifier)); i++) {
        if (strcasecmp(modifiers[i].name, name) == 0) {
            return modifiers[i].mod;
        }
    }
    return 0;
}

const char *keyboard_get_modifier_name_by_mask(uint32_t modifier)
{
    for (int i = 0; i < (int)(sizeof(modifiers) / sizeof(struct modifier)); i++) {
        if (modifiers[i].mod == modifier) {
            return modifiers[i].name;
        }
    }

    return NULL;
}

static int get_modifier_names(const char **names, uint32_t modifier_masks)
{
    int length = 0;
    for (int i = 0; i < (int)(sizeof(modifiers) / sizeof(struct modifier)); i++) {
        if ((modifier_masks & modifiers[i].mod) != 0) {
            names[length] = modifiers[i].name;
            ++length;
            modifier_masks ^= modifiers[i].mod;
        }
    }

    return length;
}

static void modifiers_mask_debug(uint32_t mask, const char *mask_name)
{
    const char *names[8];
    char name[64];
    char *p = name;

    int len = get_modifier_names(names, mask);
    for (int i = 0; i < len; i++) {
        p += sprintf(p, "%s ", names[i]);
    }
    if (len > 0) {
        *--p = '\0'; // strip last space
        kywc_log(KYWC_DEBUG, "\t %s: %s", mask_name, name);
    }
}

// TODO: compose and dead key support
static void keyboard_state_erase_key(struct keyboard_state *keyboard_state, uint32_t keysym)
{
    uint32_t idx = 0;
    for (size_t i = 0; i < keyboard_state->npressed; i++) {
        if (i > idx) {
            keyboard_state->pressed_keysyms[idx] = keyboard_state->pressed_keysyms[i];
        }
        if (keyboard_state->pressed_keysyms[i] != keysym) {
            idx++;
        }
    }

    while (keyboard_state->npressed > idx) {
        keyboard_state->npressed--;
        keyboard_state->pressed_keysyms[keyboard_state->npressed] = 0;
    }
    kywc_log(KYWC_DEBUG, "erase key %d, %lu", keysym, keyboard_state->npressed);
    for (size_t i = 0; i < keyboard_state->npressed; i++) {
        kywc_log(KYWC_DEBUG, "\t current keysym %lu: %d", i, keyboard_state->pressed_keysyms[i]);
    }
}

static void keyboard_state_add_key(struct keyboard_state *keyboard_state, uint32_t keysym)
{
    if (keyboard_state->npressed >= MAX_PRESSED_KEY) {
        return;
    }

    keyboard_state->pressed_keysyms[keyboard_state->npressed] = keysym;
    keyboard_state->npressed++;
    kywc_log(KYWC_DEBUG, "add key %d, %lu", keysym, keyboard_state->npressed);
    for (size_t i = 0; i < keyboard_state->npressed; i++) {
        kywc_log(KYWC_DEBUG, "\t current keysym %lu: %d", i, keyboard_state->pressed_keysyms[i]);
    }
}

static void handle_keyboard_state(struct keyboard_state *keyboard_state, uint32_t modifiers,
                                  uint32_t keysym, bool pressed)
{

    bool last_key_is_modifiers = modifiers != keyboard_state->last_modifiers;
    keyboard_state->last_modifiers = modifiers;

    if (last_key_is_modifiers && keyboard_state->last_keysym) {
        // a modifiier key preesed before this key, erase it
        keyboard_state_erase_key(keyboard_state, keyboard_state->last_keysym);
        keyboard_state->last_keysym = 0;
    }

    if (pressed) {
        keyboard_state_add_key(keyboard_state, keysym);
        keyboard_state->last_keysym = keysym;
    } else {
        keyboard_state_erase_key(keyboard_state, xkb_keysym_to_upper(keysym));
        keyboard_state_erase_key(keyboard_state, xkb_keysym_to_lower(keysym));
    }
}

static bool keyboard_handle_bindings(struct keyboard *keyboard, uint32_t key, bool pressed,
                                     uint32_t modifiers)
{
    struct keyboard_state *keyboard_state = &keyboard->state;

    /* Translate libinput keycode -> xkbcommon keysym */
    const xkb_keysym_t *keysyms;
    size_t keysyms_len = xkb_state_key_get_syms(keyboard_state->xkb_state, key + 8, &keysyms);

    for (size_t i = 0; i < keysyms_len; ++i) {
        handle_keyboard_state(keyboard_state, modifiers, keysyms[i], pressed);
    }

    // if (check_vt_switch(raw_key, modifiers)) {
    //    return true;
    // }

    if (pressed) {
        return bindings_handle_key_binding(keyboard_state);
    }
    return false;
}

static void keyboard_feed_key(struct keyboard *keyboard, uint32_t key, bool pressed, uint32_t time,
                              uint32_t modifiers)
{
    kywc_log(KYWC_DEBUG, "keyboard keycode %d %s", key, pressed ? "pressed" : "released");

    modifiers_mask_debug(modifiers, "modifiers");
    keyboard_handle_bindings(keyboard, key, pressed, modifiers);
}

static void keyboard_feed_modifiers(struct keyboard *keyboard, uint32_t depressed, uint32_t latched,
                                    uint32_t locked, uint32_t group)
{
    if (kywc_log_get_level() == KYWC_DEBUG) {
        kywc_log(KYWC_DEBUG, "keyboard modifiers update");
        modifiers_mask_debug(depressed, "depressed");
        modifiers_mask_debug(latched, "latched");
        modifiers_mask_debug(locked, "locked");
        modifiers_mask_debug(group, "group");
    }
}

static void keyboard_handle_key(struct wl_listener *listener, void *data)
{
    struct keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard *wlr_keyboard = keyboard->wlr_keyboard;
    struct wlr_keyboard_key_event *event = data;

    uint32_t modifiers = wlr_keyboard_get_modifiers(wlr_keyboard);
    keyboard_feed_key(keyboard, event->keycode, event->state == WL_KEYBOARD_KEY_STATE_PRESSED,
                      event->time_msec, modifiers);
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data)
{
    struct keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
    struct wlr_keyboard *wlr_keyboard = keyboard->wlr_keyboard;
    struct wlr_keyboard_modifiers *modifiers = &wlr_keyboard->modifiers;

    keyboard_feed_modifiers(keyboard, modifiers->depressed, modifiers->latched, modifiers->locked,
                            modifiers->group);
}

void keyboard_add_input(struct seat *seat, struct input *input)
{
    struct wlr_input_device *wlr_input = input->wlr_input;
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(wlr_input);
    struct wlr_keyboard *dst_keyboard = wlr_keyboard;
    struct keyboard *keyboard;

    /* virtual keyboard is not managed by group */
    if (input->prop.is_virtual) {
        goto create;
    }

    /* find a suitable group */
    wl_list_for_each(keyboard, &seat->keyboards, link) {
        if (keyboard->is_virtual) {
            continue;
        }

        dst_keyboard = keyboard->wlr_keyboard;
        struct wlr_keyboard_group *wlr_group = wlr_keyboard_group_from_wlr_keyboard(dst_keyboard);

        if (wlr_keyboard_keymaps_match(wlr_keyboard->keymap, dst_keyboard->keymap) &&
            wlr_keyboard->repeat_info.rate == dst_keyboard->repeat_info.rate &&
            wlr_keyboard->repeat_info.delay == dst_keyboard->repeat_info.delay) {
            kywc_log(KYWC_DEBUG, "Adding keyboard %s to group %p", input->name, wlr_group);
            wlr_keyboard_group_add_keyboard(wlr_group, wlr_keyboard);
            wlr_keyboard->data = wlr_group;
            return;
        }
    }

create:
    /* create a new keyboard with keyboard configuration */
    keyboard = calloc(1, sizeof(struct keyboard));
    if (!keyboard) {
        return;
    }

    /* create a new keyboard group */
    if (!input->prop.is_virtual) {
        struct wlr_keyboard_group *wlr_group = wlr_keyboard_group_create();
        dst_keyboard = &wlr_group->keyboard;
        wlr_keyboard->data = wlr_group;
        wlr_keyboard_set_keymap(dst_keyboard, wlr_keyboard->keymap);
        wlr_keyboard_set_repeat_info(dst_keyboard, wlr_keyboard->repeat_info.rate,
                                     wlr_keyboard->repeat_info.delay);
        wlr_keyboard_group_add_keyboard(wlr_group, wlr_keyboard);
    }

    keyboard->wlr_keyboard = dst_keyboard;
    dst_keyboard->data = keyboard;
    keyboard->is_virtual = input->prop.is_virtual;
    keyboard->state.xkb_state = dst_keyboard->xkb_state;

    /* insert new keyboard to seat keyboard list */
    keyboard->seat = seat;
    wl_list_insert(&seat->keyboards, &keyboard->link);

    struct wlr_seat *wlr_seat = seat->wlr_seat;
    wlr_seat_set_keyboard(wlr_seat, dst_keyboard);

    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&dst_keyboard->events.key, &keyboard->key);
    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&dst_keyboard->events.modifiers, &keyboard->modifiers);
}

void keyboard_destroy(struct keyboard *keyboard)
{
    struct wlr_seat *wlr_seat = keyboard->seat->wlr_seat;
    struct wlr_keyboard *wlr_keyboard = keyboard->wlr_keyboard;

    if (wlr_seat_get_keyboard(wlr_seat) == wlr_keyboard) {
        wlr_seat_set_keyboard(wlr_seat, NULL);
    }

    wl_list_remove(&keyboard->link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->modifiers.link);

    if (!keyboard->is_virtual) {
        struct wlr_keyboard_group *wlr_group = wlr_keyboard_group_from_wlr_keyboard(wlr_keyboard);
        wlr_keyboard_group_destroy(wlr_group);
    }

    free(keyboard);
}

void keyboard_remove_input(struct input *input)
{
    struct wlr_input_device *wlr_input = input->wlr_input;
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(wlr_input);
    struct keyboard *keyboard;

    if (input->prop.is_virtual) {
        keyboard = wlr_keyboard->data;
        keyboard_destroy(keyboard);
        return;
    }

    struct wlr_keyboard_group *wlr_group = wlr_keyboard->group;
    /* already remove when input destroy at keyboard group */
    if (!wlr_group) {
        wlr_group = wlr_keyboard->data;
    } else {
        wlr_keyboard_group_remove_keyboard(wlr_group, wlr_keyboard);
    }

    /* destroy keyboard group if empty */
    if (wl_list_empty(&wlr_group->devices)) {
        keyboard = wlr_group->keyboard.data;
        keyboard_destroy(keyboard);
    }
}
