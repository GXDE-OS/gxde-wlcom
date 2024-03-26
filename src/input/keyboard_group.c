// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <time.h>
#include <xkbcommon/xkbcommon.h>

#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard.h>

#include <kywc/log.h>

#include "input/keyboard_group.h"

struct keyboard_group_device {
    struct wlr_keyboard *keyboard;
    struct wl_listener key;
    struct wl_listener modifiers;
    struct wl_listener keymap;
    struct wl_listener repeat_info;
    struct wl_listener destroy;
    struct wl_list link; // keyboard_group.devices
};

struct keyboard_group_key {
    uint32_t keycode;
    size_t count;
    struct wl_list link; // keyboard_group.keys
};

static void keyboard_group_set_leds(struct wlr_keyboard *kb, uint32_t leds)
{
    struct keyboard_group *group = keyboard_group_from_wlr_keyboard(kb);
    uint32_t fixed_leds = leds;

    if (group->scroll_lock > 0) {
        fixed_leds |= WLR_LED_SCROLL_LOCK;
    } else {
        fixed_leds &= ~WLR_LED_SCROLL_LOCK;
    }

    struct keyboard_group_device *device;
    wl_list_for_each(device, &group->devices, link) {
        if (group->scroll_lock_led_on) {
            device->keyboard->leds |= WLR_LED_SCROLL_LOCK;
        }
        wlr_keyboard_led_update(device->keyboard, fixed_leds);
        device->keyboard->leds &= ~WLR_LED_SCROLL_LOCK;
    }

    group->scroll_lock_led_on = group->scroll_lock > 0;
}

static const struct wlr_keyboard_impl impl = {
    .name = "keyboard-group",
    .led_update = keyboard_group_set_leds,
};

struct keyboard_group *keyboard_group_create(void)
{
    struct keyboard_group *group = calloc(1, sizeof(*group));
    if (!group) {
        kywc_log(KYWC_ERROR, "Failed to allocate keyboard_group");
        return NULL;
    }

    wlr_keyboard_init(&group->keyboard, &impl, "keyboard_group");
    wl_list_init(&group->devices);
    wl_list_init(&group->keys);

    return group;
}

struct keyboard_group *keyboard_group_from_wlr_keyboard(struct wlr_keyboard *keyboard)
{
    if (keyboard->impl != &impl) {
        return NULL;
    }
    struct keyboard_group *group = wl_container_of(keyboard, group, keyboard);
    return group;
}

static bool process_key(struct keyboard_group_device *group_device,
                        struct wlr_keyboard_key_event *event)
{
    struct keyboard_group *group = (struct keyboard_group *)group_device->keyboard->group;

    struct keyboard_group_key *key, *tmp;
    wl_list_for_each_safe(key, tmp, &group->keys, link) {
        if (key->keycode != event->keycode) {
            continue;
        }
        if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            key->count++;
            return false;
        }
        if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED) {
            key->count--;
            if (key->count > 0) {
                return false;
            }
            wl_list_remove(&key->link);
            free(key);
        }
        break;
    }

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        struct keyboard_group_key *key = calloc(1, sizeof(*key));
        if (!key) {
            kywc_log(KYWC_ERROR, "Failed to allocate keyboard_group_key");
            return false;
        }
        key->keycode = event->keycode;
        key->count = 1;
        wl_list_insert(&group->keys, &key->link);
    }

    return true;
}

static void handle_keyboard_key(struct wl_listener *listener, void *data)
{
    struct keyboard_group_device *group_device = wl_container_of(listener, group_device, key);
    struct keyboard_group *group = (struct keyboard_group *)group_device->keyboard->group;
    if (process_key(group_device, data)) {
        wlr_keyboard_notify_key(&group->keyboard, data);
    }
}

static void handle_keyboard_modifiers(struct wl_listener *listener, void *data)
{
    // Sync the effective layout (group modifier) to all keyboards. The rest of
    // the modifiers will be derived from the keyboard_group's key state
    struct keyboard_group_device *group_device = wl_container_of(listener, group_device, modifiers);
    struct keyboard_group *group = (struct keyboard_group *)group_device->keyboard->group;
    struct wlr_keyboard_modifiers mods = group_device->keyboard->modifiers;

    struct keyboard_group_device *device;
    wl_list_for_each(device, &group->devices, link) {
        if (mods.depressed != device->keyboard->modifiers.depressed ||
            mods.latched != device->keyboard->modifiers.latched ||
            mods.locked != device->keyboard->modifiers.locked ||
            mods.group != device->keyboard->modifiers.group) {
            wlr_keyboard_notify_modifiers(device->keyboard, mods.depressed, mods.latched,
                                          mods.locked, mods.group);
            return;
        }
    }

    wlr_keyboard_notify_modifiers(&group->keyboard, mods.depressed, mods.latched, mods.locked,
                                  mods.group);
}

static void handle_keyboard_keymap(struct wl_listener *listener, void *data)
{
    struct keyboard_group_device *group_device = wl_container_of(listener, group_device, keymap);
    struct wlr_keyboard *keyboard = group_device->keyboard;
    struct keyboard_group *group = (struct keyboard_group *)keyboard->group;

    if (!wlr_keyboard_keymaps_match(group->keyboard.keymap, keyboard->keymap)) {
        struct keyboard_group_device *device;
        wl_list_for_each(device, &group->devices, link) {
            if (!wlr_keyboard_keymaps_match(keyboard->keymap, device->keyboard->keymap)) {
                wlr_keyboard_set_keymap(device->keyboard, keyboard->keymap);
                return;
            }
        }
    }

    wlr_keyboard_set_keymap(&group->keyboard, keyboard->keymap);
}

static void handle_keyboard_repeat_info(struct wl_listener *listener, void *data)
{
    struct keyboard_group_device *group_device =
        wl_container_of(listener, group_device, repeat_info);
    struct wlr_keyboard *keyboard = group_device->keyboard;
    struct keyboard_group *group = (struct keyboard_group *)keyboard->group;

    struct keyboard_group_device *device;
    wl_list_for_each(device, &group->devices, link) {
        struct wlr_keyboard *devkb = device->keyboard;
        if (devkb->repeat_info.rate != keyboard->repeat_info.rate ||
            devkb->repeat_info.delay != keyboard->repeat_info.delay) {
            wlr_keyboard_set_repeat_info(devkb, keyboard->repeat_info.rate,
                                         keyboard->repeat_info.delay);
            return;
        }
    }

    wlr_keyboard_set_repeat_info(&group->keyboard, keyboard->repeat_info.rate,
                                 keyboard->repeat_info.delay);
}

static void remove_keyboard_group_device(struct keyboard_group_device *device)
{
    device->keyboard->group = NULL;
    wl_list_remove(&device->link);
    wl_list_remove(&device->key.link);
    wl_list_remove(&device->modifiers.link);
    wl_list_remove(&device->keymap.link);
    wl_list_remove(&device->repeat_info.link);
    wl_list_remove(&device->destroy.link);
    free(device);
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data)
{
    struct keyboard_group_device *device = wl_container_of(listener, device, destroy);
    remove_keyboard_group_device(device);
}

bool keyboard_group_add_keyboard(struct keyboard_group *group, struct wlr_keyboard *keyboard)
{
    if (keyboard->group) {
        kywc_log(KYWC_ERROR, "A wlr_keyboard can only belong to one group");
        return false;
    }

    if (keyboard->impl == &impl) {
        kywc_log(KYWC_ERROR, "Cannot add a group's keyboard to a group");
        return false;
    }

    if (!wlr_keyboard_keymaps_match(group->keyboard.keymap, keyboard->keymap)) {
        kywc_log(KYWC_ERROR, "Device keymap does not match keyboard group's");
        return false;
    }

    struct keyboard_group_device *device = calloc(1, sizeof(*device));
    if (!device) {
        kywc_log(KYWC_ERROR, "Failed to allocate keyboard_group_device");
        return false;
    }

    device->keyboard = keyboard;
    keyboard->group = (struct wlr_keyboard_group *)group;
    wl_list_insert(&group->devices, &device->link);

    wl_signal_add(&keyboard->events.key, &device->key);
    device->key.notify = handle_keyboard_key;

    wl_signal_add(&keyboard->events.modifiers, &device->modifiers);
    device->modifiers.notify = handle_keyboard_modifiers;

    wl_signal_add(&keyboard->events.keymap, &device->keymap);
    device->keymap.notify = handle_keyboard_keymap;

    wl_signal_add(&keyboard->events.repeat_info, &device->repeat_info);
    device->repeat_info.notify = handle_keyboard_repeat_info;

    wl_signal_add(&keyboard->base.events.destroy, &device->destroy);
    device->destroy.notify = handle_keyboard_destroy;

    struct wlr_keyboard *group_kb = &group->keyboard;
    wlr_keyboard_set_repeat_info(keyboard, group_kb->repeat_info.rate, group_kb->repeat_info.delay);
    /* sync the group modifiers to keyboard */
    wlr_keyboard_notify_modifiers(keyboard, group_kb->modifiers.depressed,
                                  group_kb->modifiers.latched, group_kb->modifiers.locked,
                                  group_kb->modifiers.group);
    /* force sync leds to keyboard */
    group->scroll_lock_led_on = false;
    keyboard_group_set_leds(group_kb, group_kb->leds);
    return true;
}

void keyboard_group_remove_keyboard(struct keyboard_group *group, struct wlr_keyboard *keyboard)
{
    struct keyboard_group_device *device, *tmp;
    wl_list_for_each_safe(device, tmp, &group->devices, link) {
        if (device->keyboard == keyboard) {
            remove_keyboard_group_device(device);
            return;
        }
    }
    kywc_log(KYWC_ERROR, "keyboard not found in group");
}

void keyboard_group_destroy(struct keyboard_group *group)
{
    struct keyboard_group_device *device, *tmp;
    wl_list_for_each_safe(device, tmp, &group->devices, link) {
        keyboard_group_remove_keyboard(group, device->keyboard);
    }
    wlr_keyboard_finish(&group->keyboard);
    free(group);
}

struct wlr_keyboard *keyboard_group_pick_keyboard(struct keyboard_group *group)
{
    if (wl_list_empty(&group->devices)) {
        return &group->keyboard;
    }

    struct keyboard_group_device *device = wl_container_of(group->devices.next, device, link);
    return device->keyboard;
}
