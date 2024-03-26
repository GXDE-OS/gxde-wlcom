// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _KEYBOARD_GROUP_H_
#define _KEYBOARD_GROUP_H_

#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>

struct keyboard_group {
    struct wlr_keyboard keyboard;
    struct wl_list devices; // keyboard_group_device.link
    struct wl_list keys;    // keyboard_group_key.link

    uint32_t scroll_lock;
    bool scroll_lock_led_on;

    void *data;
};

struct keyboard_group *keyboard_group_create(void);

struct keyboard_group *keyboard_group_from_wlr_keyboard(struct wlr_keyboard *keyboard);

bool keyboard_group_add_keyboard(struct keyboard_group *group, struct wlr_keyboard *keyboard);

void keyboard_group_remove_keyboard(struct keyboard_group *group, struct wlr_keyboard *keyboard);

void keyboard_group_destroy(struct keyboard_group *group);

struct wlr_keyboard *keyboard_group_pick_keyboard(struct keyboard_group *group);

#endif
