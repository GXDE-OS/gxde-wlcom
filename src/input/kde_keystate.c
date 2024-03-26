// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <kywc/log.h>

#include <linux/input-event-codes.h>
#include <wlr/interfaces/wlr_keyboard.h>

#include "input/seat.h"
#include "input_p.h"
#include "kde_keystate-protocol.h"
#include "server.h"

#define ORG_KDE_KWIN_KEYSTATE_VERSION 4

#define SETBIT(x, y) ((x) |= (1 << (y)))
#define CLRBIT(x, y) ((x) &= ~(1 << (y)))
#define REVBIT(x, y) ((x) ^= (1 << (y)))
#define GETBIT(x, y) (((x) >> (y)) & 1)

struct keystate_manager {
    struct wl_global *global;

    uint32_t keyboard_lock;
    uint32_t release_record;

    struct wl_list resources;
    struct wl_list keyboards;

    struct wl_listener new_seat;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct keystate_keyboard {
    struct wl_list link;

    struct wl_listener key;
    struct wl_listener destroy;
};

static struct keystate_manager *keystate_manager = NULL;

static void kde_keystate_fetchstates(struct wl_client *client, struct wl_resource *resource)
{
    uint32_t state = 0;
    state = GETBIT(keystate_manager->keyboard_lock, INPUT_KEY_CAPSLOCK)
                ? ORG_KDE_KWIN_KEYSTATE_STATE_LOCKED
                : ORG_KDE_KWIN_KEYSTATE_STATE_UNLOCKED;
    org_kde_kwin_keystate_send_stateChanged(resource, ORG_KDE_KWIN_KEYSTATE_KEY_CAPSLOCK, state);

    state = GETBIT(keystate_manager->keyboard_lock, INPUT_KEY_NUMLOCK)
                ? ORG_KDE_KWIN_KEYSTATE_STATE_LOCKED
                : ORG_KDE_KWIN_KEYSTATE_STATE_UNLOCKED;
    org_kde_kwin_keystate_send_stateChanged(resource, ORG_KDE_KWIN_KEYSTATE_KEY_NUMLOCK, state);

    state = GETBIT(keystate_manager->keyboard_lock, INPUT_KEY_SCROLLLOCK)
                ? ORG_KDE_KWIN_KEYSTATE_STATE_LOCKED
                : ORG_KDE_KWIN_KEYSTATE_STATE_UNLOCKED;
    org_kde_kwin_keystate_send_stateChanged(resource, ORG_KDE_KWIN_KEYSTATE_KEY_SCROLLLOCK, state);
}

static void kde_keystate_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct org_kde_kwin_keystate_interface kde_keystate_impl = {
    .fetchStates = kde_keystate_fetchstates,
    .destroy = kde_keystate_destroy,
};

static void kde_keystate_unbind(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static void kde_keystate_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_kwin_keystate_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &kde_keystate_impl, NULL, kde_keystate_unbind);
    wl_list_insert(&keystate_manager->resources, wl_resource_get_link(resource));
}

static void keystate_keyboard_destroy(struct keystate_keyboard *keyboard)
{
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

static void handle_display_destory(struct wl_listener *listener, void *data)
{
    wl_list_remove(&keystate_manager->display_destroy.link);
    wl_list_remove(&keystate_manager->new_seat.link);

    if (keystate_manager->global) {
        wl_global_destroy(keystate_manager->global);
    }
    struct keystate_keyboard *keyboard, *tmp;
    wl_list_for_each_safe(keyboard, tmp, &keystate_manager->keyboards, link) {
        keystate_keyboard_destroy(keyboard);
    }
}

static void handle_server_destory(struct wl_listener *listener, void *data)
{
    wl_list_remove(&keystate_manager->server_destroy.link);
    free(keystate_manager);
}

static void keyboard_lock_change(enum input_lock_key key)
{
    uint32_t state = 0;

    REVBIT(keystate_manager->keyboard_lock, key);
    state = GETBIT(keystate_manager->keyboard_lock, key);
    if (state) {
        SETBIT(keystate_manager->release_record, key);
        state = ORG_KDE_KWIN_KEYSTATE_STATE_LOCKED;
    }

    struct wl_resource *resource;
    wl_resource_for_each(resource, &keystate_manager->resources) {
        org_kde_kwin_keystate_send_stateChanged(resource, key, state);
    }
}

static void keyboard_handle_key(enum input_lock_key key, struct wlr_keyboard_key_event *event)
{
    if (GETBIT(keystate_manager->release_record, key)) {
        CLRBIT(keystate_manager->release_record, key);
        return;
    }
    if (GETBIT(keystate_manager->keyboard_lock, key) != event->state) {
        keyboard_lock_change(key);
    }
}

static void handle_key(struct wl_listener *listener, void *data)
{
    struct keystate_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard_key_event *event = data;

    switch (event->keycode) {
    case KEY_CAPSLOCK:
        keyboard_handle_key(INPUT_KEY_CAPSLOCK, event);
        break;
    case KEY_NUMLOCK:
        keyboard_handle_key(INPUT_KEY_NUMLOCK, event);
        break;
    case KEY_SCROLLLOCK:
        keyboard_handle_key(INPUT_KEY_SCROLLLOCK, event);
        break;
    default:
        return;
    }
}

static void handle_destroy(struct wl_listener *listener, void *data)
{
    struct keystate_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    keystate_keyboard_destroy(keyboard);
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct seat *seat = data;
    struct wlr_keyboard *wlr_keyboard = seat->keyboard->wlr_keyboard;
    struct keystate_keyboard *keystate_keyboard = calloc(1, sizeof(struct keystate_keyboard));
    if (!keystate_keyboard) {
        return;
    }

    wl_list_insert(&keystate_manager->keyboards, &keystate_keyboard->link);

    keystate_keyboard->key.notify = handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keystate_keyboard->key);
    keystate_keyboard->destroy.notify = handle_destroy;
    wl_signal_add(&seat->events.destroy, &keystate_keyboard->destroy);
}

bool kde_keystate_manager_create(struct input_manager *input_manager)
{
    keystate_manager = calloc(1, sizeof(struct keystate_manager));
    if (!keystate_manager) {
        return false;
    }

    wl_list_init(&keystate_manager->resources);
    wl_list_init(&keystate_manager->keyboards);

    keystate_manager->global =
        wl_global_create(input_manager->server->display, &org_kde_kwin_keystate_interface,
                         ORG_KDE_KWIN_KEYSTATE_VERSION, keystate_manager, kde_keystate_bind);
    if (!keystate_manager->global) {
        kywc_log(KYWC_WARN, "%s global create failed", org_kde_kwin_keystate_interface.name);
        free(keystate_manager);
        return false;
    }

    keystate_manager->display_destroy.notify = handle_display_destory;
    wl_display_add_destroy_listener(input_manager->server->display,
                                    &keystate_manager->display_destroy);
    keystate_manager->server_destroy.notify = handle_server_destory;
    server_add_destroy_listener(input_manager->server, &keystate_manager->server_destroy);

    keystate_manager->new_seat.notify = handle_new_seat;
    wl_signal_add(&input_manager->events.new_seat, &keystate_manager->new_seat);
    return true;
}
