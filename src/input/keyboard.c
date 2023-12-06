// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>
#include <strings.h>
#include <xkbcommon/xkbcommon.h>

#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_seat.h>

#include <kywc/log.h>

#include "input/keyboard.h"
#include "input/seat.h"
#include "input_p.h"
#include "util/time.h"

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
    if (keyboard_state->npressed > 0 &&
        keyboard_state->pressed_keysyms[keyboard_state->npressed - 1] == keysym) {
        return;
    }

    keyboard_state->pressed_keysyms[keyboard_state->npressed] = keysym;
    keyboard_state->npressed++;
    kywc_log(KYWC_DEBUG, "add key %d, %lu", keysym, keyboard_state->npressed);
    for (size_t i = 0; i < keyboard_state->npressed; i++) {
        kywc_log(KYWC_DEBUG, "\t current keysym %lu: %d", i, keyboard_state->pressed_keysyms[i]);
    }
}

static void keyboard_state_clear(struct keyboard_state *keyboard_state)
{
    if (keyboard_state->npressed > 0) {
        *keyboard_state = (struct keyboard_state){ 0 };
    }
}

static void handle_keyboard_state(struct keyboard_state *keyboard_state, uint32_t modifiers,
                                  uint32_t keysym, bool pressed)
{

    bool last_key_is_modifiers = modifiers != keyboard_state->last_modifiers;

    keyboard_state->only_one_modifier = modifiers && keyboard_state->last_modifiers == 0 &&
                                        !pressed && keyboard_state->npressed == 1;

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
                                     uint32_t modifiers, bool *repeat)
{
    struct keyboard_state *keyboard_state = &keyboard->state;
    *repeat = false;

    /* Translate libinput keycode -> xkbcommon keysym */
    const xkb_keysym_t *keysyms;
    size_t len = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, key + 8, &keysyms);

    for (size_t i = 0; i < len; ++i) {
        handle_keyboard_state(keyboard_state, modifiers, keysyms[i], pressed);
    }

    if (!pressed) {
        return keyboard->is_virtual ? false : bindings_handle_key_binding(keyboard_state, repeat);
    }

    for (size_t i = 0; i < len; ++i) {
        xkb_keysym_t keysym = keysyms[i];
        if (keysym >= XKB_KEY_XF86Switch_VT_1 && keysym <= XKB_KEY_XF86Switch_VT_12) {
            input_manager_switch_vt(keysym - XKB_KEY_XF86Switch_VT_1 + 1);
            return true;
        }
    }

    return bindings_handle_key_binding(keyboard_state, repeat);
}

static void keyboard_repeat_stop(struct keyboard *keyboard)
{
    if (!keyboard->repeat.timer) {
        return;
    }
    if (keyboard->repeat.key == 0) {
        return;
    }

    keyboard->repeat.key = 0;
    wl_event_source_timer_update(keyboard->repeat.timer, 0);
}

static void keybaord_repeat_start(struct keyboard *keyboard, uint32_t key, bool pressed)
{
    if (!keyboard->repeat.timer) {
        return;
    }

    if (keyboard->repeat.key > 0) {
        if (keyboard->repeat.key == key && !pressed) {
            keyboard_repeat_stop(keyboard);
        }
        return;
    }

    /* only enable key repeat when pressed state */
    if (!pressed) {
        return;
    }

    int32_t delay = keyboard->wlr_keyboard->repeat_info.delay;
    if (delay > 0) {
        keyboard->repeat.key = key;
        if (wl_event_source_timer_update(keyboard->repeat.timer, delay) < 0) {
            kywc_log(KYWC_DEBUG, "failed to set key repeat timer");
        }
    } else if (keyboard->repeat.key > 0) {
        keyboard_repeat_stop(keyboard);
    }
}

void keyboard_feed_key(struct keyboard *keyboard, uint32_t key, bool pressed, uint32_t time,
                       uint32_t modifiers)
{
    modifiers_mask_debug(modifiers, "modifiers");

    struct seat *seat = keyboard->seat;
    uint32_t state = pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED;

    if (seat->keyboard_grab && seat->keyboard_grab->interface->key &&
        seat->keyboard_grab->interface->key(seat->keyboard_grab, time, key, pressed, modifiers)) {
        keyboard_state_clear(&keyboard->state);
        keybaord_repeat_start(keyboard, key, pressed);
        return;
    }

    /* don't auto repeat for some bindings, like vt switch */
    bool need_repeat = false;
    bool handled = keyboard_handle_bindings(keyboard, key, pressed, modifiers, &need_repeat);
    if (handled) {
        need_repeat ? keybaord_repeat_start(keyboard, key, pressed)
                    : keyboard_repeat_stop(keyboard);
        return;
    }

    keyboard_repeat_stop(keyboard);

    handled = input_method_handle_key(keyboard, time, key, state);
    if (handled) {
        return;
    }

    wlr_seat_set_keyboard(seat->wlr_seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_key(seat->wlr_seat, time, key, state);
}

static void keyboard_feed_modifiers(struct keyboard *keyboard,
                                    struct wlr_keyboard_modifiers *modifiers)
{
    if (kywc_log_get_level() == KYWC_DEBUG) {
        kywc_log(KYWC_DEBUG, "keyboard modifiers update");
        modifiers_mask_debug(modifiers->depressed, "depressed");
        modifiers_mask_debug(modifiers->latched, "latched");
        modifiers_mask_debug(modifiers->locked, "locked");
        modifiers_mask_debug(modifiers->group, "group");
    }

    if (input_method_handle_modifiers(keyboard)) {
        return;
    }

    struct wlr_seat *wlr_seat = keyboard->seat->wlr_seat;
    wlr_seat_set_keyboard(wlr_seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(wlr_seat, modifiers);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data)
{
    struct keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard *wlr_keyboard = keyboard->wlr_keyboard;
    struct seat *seat = keyboard->seat;
    struct wlr_keyboard_key_event *event = data;

    idle_manager_notify_activity(seat);

    uint32_t modifiers = wlr_keyboard_get_modifiers(wlr_keyboard);
    keyboard_feed_key(keyboard, event->keycode, event->state, event->time_msec, modifiers);
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data)
{
    struct keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
    struct wlr_keyboard *wlr_keyboard = keyboard->wlr_keyboard;

    keyboard_feed_modifiers(keyboard, &wlr_keyboard->modifiers);
}

static void keyboard_feed_fake_key(struct keyboard *keyboard, uint32_t key)
{
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
    keyboard_feed_key(keyboard, key, true, current_time_msec(), modifiers);
}

static int keyboard_handle_repeat(void *data)
{
    struct keyboard *keyboard = data;
    if (keyboard->repeat.key > 0) {
        if (keyboard->wlr_keyboard->repeat_info.rate > 0) {
            // We queue the next event first, as the command might cancel it
            if (wl_event_source_timer_update(keyboard->repeat.timer,
                                             1000 / keyboard->wlr_keyboard->repeat_info.rate) < 0) {
                kywc_log(KYWC_DEBUG, "failed to update key repeat timer");
            }
        }
        keyboard_feed_fake_key(keyboard, keyboard->repeat.key);
    }
    return 0;
}

struct keyboard *keyboard_create(struct seat *seat, struct wlr_keyboard *wlr_keyboard)
{
    struct keyboard *keyboard = calloc(1, sizeof(struct keyboard));
    if (!keyboard) {
        return NULL;
    }

    if (!wlr_keyboard) {
        struct wlr_keyboard_group *wlr_group = wlr_keyboard_group_create();
        keyboard->wlr_keyboard = &wlr_group->keyboard;
    } else {
        keyboard->wlr_keyboard = wlr_keyboard;
    }

    keyboard->is_virtual = !!wlr_keyboard;
    keyboard->wlr_keyboard->data = keyboard;

    /* insert new keyboard to seat keyboard list */
    keyboard->seat = seat;
    wl_list_insert(&seat->keyboards, &keyboard->link);
    wlr_seat_set_keyboard(seat->wlr_seat, keyboard->wlr_keyboard);

    /* create timer for internal key repeat */
    if (!keyboard->is_virtual) {
        struct wl_event_loop *loop = wl_display_get_event_loop(seat->wlr_seat->display);
        keyboard->repeat.timer = wl_event_loop_add_timer(loop, keyboard_handle_repeat, keyboard);
    }

    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&keyboard->wlr_keyboard->events.key, &keyboard->key);
    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&keyboard->wlr_keyboard->events.modifiers, &keyboard->modifiers);

    return keyboard;
}

void keyboard_add_input(struct seat *seat, struct input *input)
{
    struct wlr_input_device *wlr_input = input->wlr_input;
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(wlr_input);

    /* virtual keyboard is not managed by group */
    if (input->prop.is_virtual) {
        keyboard_create(seat, wlr_keyboard);
        return;
    }

    /* find a suitable group */
    struct keyboard *keyboard;
    wl_list_for_each(keyboard, &seat->keyboards, link) {
        if (keyboard->is_virtual) {
            continue;
        }

        struct wlr_keyboard *dst_keyboard = keyboard->wlr_keyboard;
        struct wlr_keyboard_group *wlr_group = wlr_keyboard_group_from_wlr_keyboard(dst_keyboard);
        bool empty_group = wl_list_empty(&wlr_group->devices);

        if (empty_group ||
            (wlr_keyboard_keymaps_match(wlr_keyboard->keymap, dst_keyboard->keymap) &&
             wlr_keyboard->repeat_info.rate == dst_keyboard->repeat_info.rate &&
             wlr_keyboard->repeat_info.delay == dst_keyboard->repeat_info.delay)) {
            kywc_log(KYWC_DEBUG, "Adding keyboard %s to group %p", input->name, wlr_group);

            if (empty_group) {
                wlr_keyboard_set_keymap(dst_keyboard, wlr_keyboard->keymap);
                wlr_keyboard_set_repeat_info(dst_keyboard, wlr_keyboard->repeat_info.rate,
                                             wlr_keyboard->repeat_info.delay);
            }

            wlr_keyboard_group_add_keyboard(wlr_group, wlr_keyboard);
            wlr_keyboard->data = wlr_group;
            return;
        }
    }

    /* create a new keyboard group with keyboard configuration */
    keyboard = keyboard_create(seat, NULL);
    if (!keyboard) {
        return;
    }

    struct wlr_keyboard_group *wlr_group =
        wlr_keyboard_group_from_wlr_keyboard(keyboard->wlr_keyboard);
    wlr_keyboard->data = wlr_group;

    wlr_keyboard_set_keymap(keyboard->wlr_keyboard, wlr_keyboard->keymap);
    wlr_keyboard_set_repeat_info(keyboard->wlr_keyboard, wlr_keyboard->repeat_info.rate,
                                 wlr_keyboard->repeat_info.delay);
    wlr_keyboard_group_add_keyboard(wlr_group, wlr_keyboard);
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

    if (keyboard->repeat.timer) {
        wl_event_source_remove(keyboard->repeat.timer);
    }

    if (keyboard->seat->keyboard == keyboard) {
        keyboard->seat->keyboard = NULL;
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
        if (keyboard != keyboard->seat->keyboard) {
            keyboard_destroy(keyboard);
        }
    }
}
