// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_seat.h>

#include <kywc/log.h>

#include "text-input-unstable-v1-protocol.h"
#include "text_input_v1.h"

static void text_input_destroy(struct text_input_v1 *text_input)
{
    wl_signal_emit_mutable(&text_input->events.destroy, NULL);
    wl_resource_set_user_data(text_input->resource, NULL);
    wl_list_remove(&text_input->surface_destroy.link);
    wl_list_remove(&text_input->seat_destroy.link);
    wl_list_remove(&text_input->link);
    free(text_input->surrounding.text);
    free(text_input);
}

static void text_input_activate(struct wl_client *client, struct wl_resource *resource,
                                struct wl_resource *seat, struct wl_resource *surface)
{
    struct text_input_v1 *text_input = wl_resource_get_user_data(resource);
    if (!text_input) {
        return;
    }
    struct wlr_seat_client *seat_client = wlr_seat_client_from_resource(seat);
    if (!seat_client) {
        return;
    }

    text_input->seat = seat_client->seat;
    wl_signal_add(&seat_client->events.destroy, &text_input->seat_destroy);
    text_input->surface = wlr_surface_from_resource(surface);
    wl_signal_add(&text_input->surface->events.destroy, &text_input->surface_destroy);

    text_input->activated = true;
    wl_signal_emit_mutable(&text_input->events.activate, NULL);
}

static void text_input_deactivate(struct wl_client *client, struct wl_resource *resource,
                                  struct wl_resource *seat)
{
    struct text_input_v1 *text_input = wl_resource_get_user_data(resource);
    if (!text_input) {
        return;
    }
    struct wlr_seat_client *seat_client = wlr_seat_client_from_resource(seat);
    if (!seat_client) {
        return;
    }
    if (text_input->seat != seat_client->seat) {
        return;
    }

    text_input->activated = false;
    wl_signal_emit_mutable(&text_input->events.deactivate, NULL);

    text_input->surface = NULL;
    text_input->seat = NULL;
    wl_list_remove(&text_input->surface_destroy.link);
    wl_list_init(&text_input->surface_destroy.link);
    wl_list_remove(&text_input->seat_destroy.link);
    wl_list_init(&text_input->seat_destroy.link);
}

static void text_input_show_input_panel(struct wl_client *client, struct wl_resource *resource)
{
    // Not implemented yet
}

static void text_input_hide_input_panel(struct wl_client *client, struct wl_resource *resource)
{
    // Not implemented yet
}

static void text_input_reset(struct wl_client *client, struct wl_resource *resource)
{
    struct text_input_v1 *text_input = wl_resource_get_user_data(resource);
    if (!text_input) {
        return;
    }

    text_input->surrounding.pending = false;
    text_input->content_type.pending = false;
}

static void text_input_set_surrounding_text(struct wl_client *client, struct wl_resource *resource,
                                            const char *text, uint32_t cursor, uint32_t anchor)
{
    struct text_input_v1 *text_input = wl_resource_get_user_data(resource);
    if (!text_input) {
        return;
    }

    free(text_input->surrounding.text);
    text_input->surrounding.text = strdup(text);
    if (!text_input->surrounding.text) {
        wl_client_post_no_memory(client);
    }
    text_input->surrounding.cursor = cursor;
    text_input->surrounding.anchor = anchor;
    text_input->surrounding.pending = true;
}

static void text_input_set_content_type(struct wl_client *client, struct wl_resource *resource,
                                        uint32_t hint, uint32_t purpose)
{
    struct text_input_v1 *text_input = wl_resource_get_user_data(resource);
    if (!text_input) {
        return;
    }

    /* convert text_input_v1 to text_input_v3 */
    text_input->content_type.hint =
        hint == ZWP_TEXT_INPUT_V1_CONTENT_HINT_DEFAULT ? ZWP_TEXT_INPUT_V1_CONTENT_HINT_NONE : hint;
    text_input->content_type.purpose =
        purpose > ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_PASSWORD ? purpose + 1 : purpose;
    text_input->content_type.pending = true;
}

static void text_input_set_cursor_rectangle(struct wl_client *client, struct wl_resource *resource,
                                            int32_t x, int32_t y, int32_t width, int32_t height)
{
    struct text_input_v1 *text_input = wl_resource_get_user_data(resource);
    if (!text_input) {
        return;
    }

    text_input->cursor_rectangle.x = x;
    text_input->cursor_rectangle.y = y;
    text_input->cursor_rectangle.width = width;
    text_input->cursor_rectangle.height = height;
}

static void text_input_set_preferred_language(struct wl_client *client,
                                              struct wl_resource *resource, const char *language)
{
    // Not implemented yet
}

static void text_input_commit_state(struct wl_client *client, struct wl_resource *resource,
                                    uint32_t serial)
{
    struct text_input_v1 *text_input = wl_resource_get_user_data(resource);
    if (!text_input) {
        return;
    }
    if (text_input->surface == NULL) {
        kywc_log(KYWC_DEBUG, "Text input commit when surface destroyed");
    }

    text_input->serial = serial;
    wl_signal_emit_mutable(&text_input->events.commit, NULL);
}

static void text_input_invoke_action(struct wl_client *client, struct wl_resource *resource,
                                     uint32_t button, uint32_t index)
{
    // Not implemented yet
}

static const struct zwp_text_input_v1_interface text_input_impl = {
    .activate = text_input_activate,
    .deactivate = text_input_deactivate,
    .show_input_panel = text_input_show_input_panel,
    .hide_input_panel = text_input_hide_input_panel,
    .reset = text_input_reset,
    .set_surrounding_text = text_input_set_surrounding_text,
    .set_content_type = text_input_set_content_type,
    .set_cursor_rectangle = text_input_set_cursor_rectangle,
    .set_preferred_language = text_input_set_preferred_language,
    .commit_state = text_input_commit_state,
    .invoke_action = text_input_invoke_action,
};

static void text_input_resource_destroy(struct wl_resource *resource)
{
    struct text_input_v1 *text_input = wl_resource_get_user_data(resource);
    if (!text_input) {
        return;
    }
    text_input_destroy(text_input);
}

static void text_input_handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct text_input_v1 *text_input = wl_container_of(listener, text_input, surface_destroy);
    text_input_destroy(text_input);
}

static void text_input_handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct text_input_v1 *text_input = wl_container_of(listener, text_input, seat_destroy);
    text_input_destroy(text_input);
}

static void text_input_manager_create_text_input(struct wl_client *client,
                                                 struct wl_resource *resource, uint32_t id)
{
    int version = wl_resource_get_version(resource);
    struct wl_resource *text_input_resource =
        wl_resource_create(client, &zwp_text_input_v1_interface, version, id);
    if (text_input_resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(text_input_resource, &text_input_impl, NULL,
                                   text_input_resource_destroy);

    struct text_input_v1 *text_input = calloc(1, sizeof(struct text_input_v1));
    if (!text_input) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_signal_init(&text_input->events.activate);
    wl_signal_init(&text_input->events.deactivate);
    wl_signal_init(&text_input->events.commit);
    wl_signal_init(&text_input->events.destroy);

    text_input->resource = text_input_resource;
    wl_resource_set_user_data(text_input_resource, text_input);

    text_input->seat_destroy.notify = text_input_handle_seat_destroy;
    wl_list_init(&text_input->seat_destroy.link);
    text_input->surface_destroy.notify = text_input_handle_surface_destroy;
    wl_list_init(&text_input->surface_destroy.link);

    struct text_input_manager_v1 *manager = wl_resource_get_user_data(resource);
    wl_list_insert(&manager->text_inputs, &text_input->link);

    wl_signal_emit_mutable(&manager->events.text_input, text_input);
}

static const struct zwp_text_input_manager_v1_interface text_input_manager_impl = {
    .create_text_input = text_input_manager_create_text_input,
};

static void text_input_manager_bind(struct wl_client *wl_client, void *data, uint32_t version,
                                    uint32_t id)
{
    struct text_input_manager_v1 *manager = data;
    assert(wl_client && manager);

    struct wl_resource *resource =
        wl_resource_create(wl_client, &zwp_text_input_manager_v1_interface, version, id);
    if (resource == NULL) {
        wl_client_post_no_memory(wl_client);
        return;
    }

    wl_resource_set_implementation(resource, &text_input_manager_impl, manager, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct text_input_manager_v1 *manager = wl_container_of(listener, manager, display_destroy);
    wl_signal_emit_mutable(&manager->events.destroy, manager);
    wl_list_remove(&manager->display_destroy.link);
    wl_global_destroy(manager->global);
    free(manager);
}

struct text_input_manager_v1 *text_input_manager_v1_create(struct wl_display *display)
{
    struct text_input_manager_v1 *manager = calloc(1, sizeof(struct text_input_manager_v1));
    if (!manager) {
        return NULL;
    }

    wl_list_init(&manager->text_inputs);
    wl_signal_init(&manager->events.text_input);
    wl_signal_init(&manager->events.destroy);

    manager->global = wl_global_create(display, &zwp_text_input_manager_v1_interface, 1, manager,
                                       text_input_manager_bind);
    if (!manager->global) {
        free(manager);
        return NULL;
    }

    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(display, &manager->display_destroy);

    return manager;
}

void text_input_v1_send_enter(struct text_input_v1 *text_input, struct wlr_surface *surface)
{
    assert(surface == text_input->surface);
    zwp_text_input_v1_send_enter(text_input->resource, text_input->surface->resource);
}

void text_input_v1_send_leave(struct text_input_v1 *text_input)
{
    zwp_text_input_v1_send_leave(text_input->resource);
    text_input->surface = NULL;
    wl_list_remove(&text_input->surface_destroy.link);
    wl_list_init(&text_input->surface_destroy.link);
}

void text_input_v1_send_preedit_string(struct text_input_v1 *text_input, const char *text,
                                       int32_t cursor_begin)
{
    zwp_text_input_v1_send_preedit_cursor(text_input->resource, cursor_begin);
    zwp_text_input_v1_send_preedit_styling(text_input->resource, 0, text ? strlen(text) : 0,
                                           ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_DEFAULT);
    zwp_text_input_v1_send_preedit_string(text_input->resource, text_input->serial,
                                          text ? text : "", "");
}

void text_input_v1_send_commit_string(struct text_input_v1 *text_input, const char *text)
{
    zwp_text_input_v1_send_commit_string(text_input->resource, text_input->serial, text);
}

void text_input_v1_send_delete_surrounding_text(struct text_input_v1 *text_input, const char *text,
                                                uint32_t before_length, uint32_t after_length)
{
    zwp_text_input_v1_send_delete_surrounding_text(
        text_input->resource, strlen(text) - before_length, after_length + before_length);
    text_input_v1_send_commit_string(text_input, text);
}
