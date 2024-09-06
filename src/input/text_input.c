// SPDX-FileCopyrightText: 2016-2017 Drew DeVault
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <assert.h>
#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>

#include <kywc/log.h>

#include "input/event.h"
#include "input_p.h"
#include "output.h"
#include "scene/surface.h"
#include "server.h"
#include "text_input_v1.h"
#include "text_input_v2.h"
#include "view/view.h"

/* most codes are copied from sway input/text_input.c */

struct input_method_manager {
    struct wlr_input_method_manager_v2 *input_method;

    struct text_input_manager_v1 *text_input_v1;
    struct wl_listener new_text_input_v1;
    struct wl_listener text_input_v1_destroy;

    struct text_input_manager_v2 *text_input_v2;
    struct wlr_text_input_manager_v3 *text_input_v3;

    struct wl_listener new_seat;
    struct wl_listener server_destroy;
};

/* input method per seat */
struct input_method_relay {
    struct seat *seat;
    struct wl_listener seat_destroy;

    struct wl_list text_inputs;
    struct wl_listener new_text_input_v2;
    struct wl_listener text_input_v2_destroy;
    struct wl_listener new_text_input_v3;
    struct wl_listener text_input_v3_destroy;

    struct wlr_input_method_v2 *wlr_input_method;
    struct wl_listener input_method_v2_destroy;
    struct wl_listener new_input_method;

    struct wl_listener input_method_commit;
    struct wl_listener input_method_grab_keyboard;
    struct wl_listener input_method_keyboard_grab_destroy;
    struct wl_listener input_method_destroy;

    struct wl_list input_popups;
    struct wl_listener new_popup_surface;

    struct text_input *pending_enabled_text_input;
};

struct text_input {
    struct text_input_v1 *text_input_v1;
    struct text_input_v2 *text_input_v2;
    struct wlr_text_input_v3 *text_input_v3;

    struct input_method_relay *relay;
    struct wl_list link;

    struct wl_listener text_input_enable;
    struct wl_listener text_input_commit;
    struct wl_listener text_input_disable;
    struct wl_listener text_input_destroy;

    struct wlr_surface *pending_focused_surface;
    struct wl_listener pending_focused_surface_destroy;
};

struct input_popup {
    struct wlr_input_popup_surface_v2 *popup_surface;
    struct input_method_relay *relay;
    struct wl_list link;

    struct ky_scene_node *surface_node;

    /* not map/unmap handle in scene surface */
    struct wl_listener surface_map;
    struct wl_listener surface_commit;
    struct wl_listener surface_unmap;
    struct wl_listener popup_surface_destroy;
};

static struct wlr_surface *text_input_focused_surface(struct text_input *text_input)
{
    if (text_input->text_input_v1) {
        return text_input->text_input_v1->surface;
    } else if (text_input->text_input_v2) {
        return text_input->text_input_v2->surface;
    } else {
        return text_input->text_input_v3->focused_surface;
    }
}

static void text_input_send_leave(struct text_input *text_input)
{
    if (text_input->text_input_v3) {
        wlr_text_input_v3_send_leave(text_input->text_input_v3);
    } else if (text_input->text_input_v2) {
        text_input_v2_send_leave(text_input->text_input_v2);
    } else {
        text_input_v1_send_leave(text_input->text_input_v1);
    }
}

static void text_input_send_enter(struct text_input *text_input, struct wlr_surface *surface)
{
    if (text_input->text_input_v3) {
        wlr_text_input_v3_send_enter(text_input->text_input_v3, surface);
    } else if (text_input->text_input_v2) {
        text_input_v2_send_enter(text_input->text_input_v2, surface);
    } else {
        text_input_v1_send_enter(text_input->text_input_v1, surface);
    }
}

static struct text_input *relay_get_focused_text_input(struct input_method_relay *relay)
{
    struct text_input *text_input = NULL;
    wl_list_for_each(text_input, &relay->text_inputs, link) {
        if (text_input_focused_surface(text_input)) {
            return text_input;
        }
    }
    return NULL;
}

static void input_popup_update(struct input_popup *popup, struct seat *seat)
{
    if (!popup->popup_surface->surface->mapped) {
        return;
    }

    struct text_input *text_input = relay_get_focused_text_input(popup->relay);
    if (!text_input) {
        return;
    }

    struct wlr_surface *focused_surface = text_input_focused_surface(text_input);
    /* workaround: view_destroy is emited before surface_destroy */
    if (!focused_surface || !focused_surface->mapped) {
        return;
    }

    struct ky_scene_buffer *scene_buffer = ky_scene_buffer_try_from_surface(focused_surface);
    assert(scene_buffer);
    /* surface primary output */
    if (!scene_buffer->primary_output) {
        return;
    }

    struct kywc_box *output_box =
        &output_from_wlr_output(scene_buffer->primary_output->output)->geometry;

    /* focused surface geometry in layout coord */
    struct kywc_box parent_box;
    ky_scene_node_coords(&scene_buffer->node, &parent_box.x, &parent_box.y);
    parent_box.width = focused_surface->current.width;
    parent_box.height = focused_surface->current.height;

    struct wlr_box cursor_box = { 0 };
    if (text_input->text_input_v3) {
        cursor_box = text_input->text_input_v3->current.cursor_rectangle;
    } else if (text_input->text_input_v2) {
        cursor_box = text_input->text_input_v2->cursor_rectangle;
    } else {
        cursor_box = text_input->text_input_v1->cursor_rectangle;
    }
    int surface_width = popup->popup_surface->surface->current.width;
    int surface_height = popup->popup_surface->surface->current.height;

    /* constraints for output */
    int dx = cursor_box.x + parent_box.x + surface_width - output_box->x - output_box->width;
    int dy = cursor_box.y + cursor_box.height + parent_box.y + surface_height - output_box->y -
             output_box->height;
    if (dx > 0) {
        cursor_box.x -= dx;
    }
    if (dy > 0) {
        cursor_box.y -= cursor_box.height + surface_height;
    }
    wlr_input_popup_surface_v2_send_text_input_rectangle(popup->popup_surface, &cursor_box);

    int x = cursor_box.x + parent_box.x;
    int y = cursor_box.y + cursor_box.height + parent_box.y;
    ky_scene_node_set_position(popup->surface_node, x, y);
}

static void relay_send_input_method_state(struct input_method_relay *relay,
                                          struct text_input *text_input)
{
    struct wlr_input_method_v2 *input_method = relay->wlr_input_method;

    // TODO: only send each of those if they were modified
    if (text_input->text_input_v3) {
        if (text_input->text_input_v3->active_features &
            WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) {
            wlr_input_method_v2_send_surrounding_text(
                input_method, text_input->text_input_v3->current.surrounding.text,
                text_input->text_input_v3->current.surrounding.cursor,
                text_input->text_input_v3->current.surrounding.anchor);
        }
        wlr_input_method_v2_send_text_change_cause(
            input_method, text_input->text_input_v3->current.text_change_cause);
        if (text_input->text_input_v3->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE) {
            wlr_input_method_v2_send_content_type(
                input_method, text_input->text_input_v3->current.content_type.hint,
                text_input->text_input_v3->current.content_type.purpose);
        }
    } else if (text_input->text_input_v2) {
        if (text_input->text_input_v2->surrounding.pending) {
            wlr_input_method_v2_send_surrounding_text(
                input_method, text_input->text_input_v2->surrounding.text,
                text_input->text_input_v2->surrounding.cursor,
                text_input->text_input_v2->surrounding.anchor);
        }
        wlr_input_method_v2_send_text_change_cause(input_method, 0);
        if (text_input->text_input_v2->content_type.pending) {
            wlr_input_method_v2_send_content_type(input_method,
                                                  text_input->text_input_v2->content_type.hint,
                                                  text_input->text_input_v2->content_type.purpose);
        }
    } else {
        if (text_input->text_input_v1->surrounding.pending) {
            wlr_input_method_v2_send_surrounding_text(
                input_method, text_input->text_input_v1->surrounding.text,
                text_input->text_input_v1->surrounding.cursor,
                text_input->text_input_v1->surrounding.anchor);
        }
        wlr_input_method_v2_send_text_change_cause(input_method, 0);
        if (text_input->text_input_v1->content_type.pending) {
            wlr_input_method_v2_send_content_type(input_method,
                                                  text_input->text_input_v1->content_type.hint,
                                                  text_input->text_input_v1->content_type.purpose);
        }
    }

    struct input_popup *popup;
    wl_list_for_each(popup, &relay->input_popups, link) {
        input_popup_update(popup, relay->seat);
    }

    wlr_input_method_v2_send_done(input_method);
}

static void handle_text_input_enable(struct wl_listener *listener, void *data)
{
    struct text_input *text_input = wl_container_of(listener, text_input, text_input_enable);

#if 0
    if (text_input->text_input_v1) {
        assert(!text_input->relay);
        assert(text_input->text_input_v1->seat);
        text_input->relay = seat_from_wlr_seat(text_input->text_input_v1->seat)->relay;
        wl_list_insert(&text_input->relay->text_inputs, &text_input->link);
    }
#endif

    if (text_input->relay->wlr_input_method == NULL) {
        text_input->relay->pending_enabled_text_input = text_input;
        kywc_log(KYWC_INFO, "Enabling text input when input method is gone");
        return;
    }

    wlr_input_method_v2_send_activate(text_input->relay->wlr_input_method);
    relay_send_input_method_state(text_input->relay, text_input);
}

static bool text_input_is_enabeld(struct text_input *text_input)
{
    if (text_input->text_input_v3) {
        return text_input->text_input_v3->current_enabled;
    } else if (text_input->text_input_v2) {
        return text_input->text_input_v2->enabled;
    } else {
        return text_input->text_input_v1->activated;
    }
}

static void handle_text_input_commit(struct wl_listener *listener, void *data)
{
    struct text_input *text_input = wl_container_of(listener, text_input, text_input_commit);
    if (!text_input_is_enabeld(text_input)) {
        kywc_log(KYWC_DEBUG, "Inactive text input tried to commit an update");
        return;
    }
    // kywc_log(KYWC_DEBUG, "Text input committed update");
    if (text_input->relay->wlr_input_method == NULL) {
        kywc_log(KYWC_INFO, "Text input committed, but input method is gone");
        return;
    }

    relay_send_input_method_state(text_input->relay, text_input);
}

static void relay_disable_text_input(struct input_method_relay *relay,
                                     struct text_input *text_input)
{
    if (relay->wlr_input_method == NULL) {
        kywc_log(KYWC_DEBUG, "Disabling text input, but input method is gone");
        return;
    }
    wlr_input_method_v2_send_deactivate(relay->wlr_input_method);
    relay_send_input_method_state(relay, text_input);
#if 0
    /* clear seat relay and updated when enable */
    if (text_input->text_input_v1) {
        wl_list_remove(&text_input->link);
        wl_list_init(&text_input->link);
        text_input->relay = NULL;
    }
#endif
}

static void handle_text_input_disable(struct wl_listener *listener, void *data)
{
    struct text_input *text_input = wl_container_of(listener, text_input, text_input_disable);

    if (text_input->relay->pending_enabled_text_input == text_input) {
        text_input->relay->pending_enabled_text_input = NULL;
    }

    if (!text_input_focused_surface(text_input)) {
        kywc_log(KYWC_DEBUG, "Disabling text input, but no longer focused");
        return;
    }

    relay_disable_text_input(text_input->relay, text_input);
}

static void text_input_set_pending_focused_surface(struct text_input *text_input,
                                                   struct wlr_surface *surface)
{
    wl_list_remove(&text_input->pending_focused_surface_destroy.link);
    text_input->pending_focused_surface = surface;

    if (surface) {
        wl_signal_add(&surface->events.destroy, &text_input->pending_focused_surface_destroy);
    } else {
        wl_list_init(&text_input->pending_focused_surface_destroy.link);
    }
}

static void handle_text_input_destroy(struct wl_listener *listener, void *data)
{
    struct text_input *text_input = wl_container_of(listener, text_input, text_input_destroy);

    if (text_input_is_enabeld(text_input)) {
        relay_disable_text_input(text_input->relay, text_input);
    }
    text_input_set_pending_focused_surface(text_input, NULL);

    if (text_input->relay->pending_enabled_text_input == text_input) {
        text_input->relay->pending_enabled_text_input = NULL;
    }

    wl_list_remove(&text_input->link);
    wl_list_remove(&text_input->text_input_commit.link);
    wl_list_remove(&text_input->text_input_destroy.link);
    wl_list_remove(&text_input->text_input_disable.link);
    wl_list_remove(&text_input->text_input_enable.link);

    free(text_input);
}

static void handle_pending_focused_surface_destroy(struct wl_listener *listener, void *data)
{
    struct text_input *text_input =
        wl_container_of(listener, text_input, pending_focused_surface_destroy);

    struct wlr_surface *surface = data;
    assert(text_input->pending_focused_surface == surface);

    text_input->pending_focused_surface = NULL;
    wl_list_remove(&text_input->pending_focused_surface_destroy.link);
    wl_list_init(&text_input->pending_focused_surface_destroy.link);
}

static void handle_new_text_input_v3(struct wl_listener *listener, void *data)
{
    struct input_method_relay *relay = wl_container_of(listener, relay, new_text_input_v3);
    struct wlr_text_input_v3 *wlr_text_input = data;
    if (relay->seat->wlr_seat != wlr_text_input->seat) {
        return;
    }

    struct text_input *text_input = calloc(1, sizeof(struct text_input));
    if (!text_input) {
        return;
    }

    text_input->text_input_v3 = wlr_text_input;
    text_input->relay = relay;
    wl_list_insert(&relay->text_inputs, &text_input->link);

    text_input->text_input_enable.notify = handle_text_input_enable;
    wl_signal_add(&wlr_text_input->events.enable, &text_input->text_input_enable);
    text_input->text_input_commit.notify = handle_text_input_commit;
    wl_signal_add(&wlr_text_input->events.commit, &text_input->text_input_commit);
    text_input->text_input_disable.notify = handle_text_input_disable;
    wl_signal_add(&wlr_text_input->events.disable, &text_input->text_input_disable);
    text_input->text_input_destroy.notify = handle_text_input_destroy;
    wl_signal_add(&wlr_text_input->events.destroy, &text_input->text_input_destroy);

    text_input->pending_focused_surface_destroy.notify = handle_pending_focused_surface_destroy;
    wl_list_init(&text_input->pending_focused_surface_destroy.link);
}

static void handle_new_text_input_v1(struct wl_listener *listener, void *data)
{
    struct text_input *text_input = calloc(1, sizeof(struct text_input));
    if (!text_input) {
        return;
    }

    struct text_input_v1 *text_input_v1 = data;
    text_input->text_input_v1 = text_input_v1;
    /* no seat set */
    // wl_list_init(&text_input->link);
    // TODO: multi-seat not support
    text_input->relay = input_manager_get_default_seat()->relay;
    wl_list_insert(&text_input->relay->text_inputs, &text_input->link);

    text_input->text_input_enable.notify = handle_text_input_enable;
    wl_signal_add(&text_input_v1->events.activate, &text_input->text_input_enable);
    text_input->text_input_commit.notify = handle_text_input_commit;
    wl_signal_add(&text_input_v1->events.commit, &text_input->text_input_commit);
    text_input->text_input_disable.notify = handle_text_input_disable;
    wl_signal_add(&text_input_v1->events.deactivate, &text_input->text_input_disable);
    text_input->text_input_destroy.notify = handle_text_input_destroy;
    wl_signal_add(&text_input_v1->events.destroy, &text_input->text_input_destroy);

    text_input->pending_focused_surface_destroy.notify = handle_pending_focused_surface_destroy;
    wl_list_init(&text_input->pending_focused_surface_destroy.link);
}

static void handle_new_text_input_v2(struct wl_listener *listener, void *data)
{
    struct input_method_relay *relay = wl_container_of(listener, relay, new_text_input_v2);
    struct text_input_v2 *text_input_v2 = data;
    if (relay->seat->wlr_seat != text_input_v2->seat) {
        return;
    }

    struct text_input *text_input = calloc(1, sizeof(struct text_input));
    if (!text_input) {
        return;
    }

    text_input->text_input_v2 = text_input_v2;
    text_input->relay = relay;
    wl_list_insert(&relay->text_inputs, &text_input->link);

    text_input->text_input_enable.notify = handle_text_input_enable;
    wl_signal_add(&text_input_v2->events.enable, &text_input->text_input_enable);
    text_input->text_input_commit.notify = handle_text_input_commit;
    wl_signal_add(&text_input_v2->events.commit, &text_input->text_input_commit);
    text_input->text_input_disable.notify = handle_text_input_disable;
    wl_signal_add(&text_input_v2->events.disable, &text_input->text_input_disable);
    text_input->text_input_destroy.notify = handle_text_input_destroy;
    wl_signal_add(&text_input_v2->events.destroy, &text_input->text_input_destroy);

    text_input->pending_focused_surface_destroy.notify = handle_pending_focused_surface_destroy;
    wl_list_init(&text_input->pending_focused_surface_destroy.link);
}

static void handle_input_method_commit(struct wl_listener *listener, void *data)
{
    struct input_method_relay *relay = wl_container_of(listener, relay, input_method_commit);
    struct text_input *text_input = relay_get_focused_text_input(relay);
    if (!text_input) {
        return;
    }

    struct wlr_input_method_v2 *context = data;
    assert(context == relay->wlr_input_method);

    if (text_input->text_input_v3) {
        if (context->current.preedit.text) {
            wlr_text_input_v3_send_preedit_string(
                text_input->text_input_v3, context->current.preedit.text,
                context->current.preedit.cursor_begin, context->current.preedit.cursor_end);
        }
        if (context->current.commit_text) {
            wlr_text_input_v3_send_commit_string(text_input->text_input_v3,
                                                 context->current.commit_text);
        }
        if (context->current.delete.before_length || context->current.delete.after_length) {
            wlr_text_input_v3_send_delete_surrounding_text(text_input->text_input_v3,
                                                           context->current.delete.before_length,
                                                           context->current.delete.after_length);
        }
        wlr_text_input_v3_send_done(text_input->text_input_v3);
    } else if (text_input->text_input_v2) {
        text_input_v2_send_preedit_string(text_input->text_input_v2, context->current.preedit.text,
                                          context->current.preedit.cursor_begin);
        if (context->current.commit_text) {
            text_input_v2_send_commit_string(text_input->text_input_v2,
                                             context->current.commit_text);
        }
        if (context->current.delete.before_length || context->current.delete.after_length) {
            text_input_v2_send_delete_surrounding_text(
                text_input->text_input_v2, context->current.preedit.text,
                context->current.delete.before_length, context->current.delete.after_length);
        }
    } else {
        text_input_v1_send_preedit_string(text_input->text_input_v1, context->current.preedit.text,
                                          context->current.preedit.cursor_begin);
        if (context->current.commit_text) {
            text_input_v1_send_commit_string(text_input->text_input_v1,
                                             context->current.commit_text);
        }
        if (context->current.delete.before_length || context->current.delete.after_length) {
            text_input_v1_send_delete_surrounding_text(
                text_input->text_input_v1, context->current.preedit.text,
                context->current.delete.before_length, context->current.delete.after_length);
        }
    }
}

static void handle_input_method_keyboard_grab_destroy(struct wl_listener *listener, void *data)
{
    struct input_method_relay *relay =
        wl_container_of(listener, relay, input_method_keyboard_grab_destroy);
    struct wlr_input_method_keyboard_grab_v2 *keyboard_grab = data;
    wl_list_remove(&relay->input_method_keyboard_grab_destroy.link);

    if (keyboard_grab->keyboard) {
        // send modifier state to original client
        wlr_seat_keyboard_notify_modifiers(keyboard_grab->input_method->seat,
                                           &keyboard_grab->keyboard->modifiers);
    }
}

static void handle_input_method_grab_keyboard(struct wl_listener *listener, void *data)
{
    struct input_method_relay *relay = wl_container_of(listener, relay, input_method_grab_keyboard);
    struct wlr_input_method_keyboard_grab_v2 *keyboard_grab = data;

    // send modifier state to grab
    struct wlr_keyboard *active_keyboard = wlr_seat_get_keyboard(relay->seat->wlr_seat);
    wlr_input_method_keyboard_grab_v2_set_keyboard(keyboard_grab, active_keyboard);

    relay->input_method_keyboard_grab_destroy.notify = handle_input_method_keyboard_grab_destroy;
    wl_signal_add(&keyboard_grab->events.destroy, &relay->input_method_keyboard_grab_destroy);
}

static void handle_input_method_destroy(struct wl_listener *listener, void *data)
{
    struct input_method_relay *relay = wl_container_of(listener, relay, input_method_destroy);
    struct wlr_input_method_v2 *context = data;
    assert(context == relay->wlr_input_method);
    relay->wlr_input_method = NULL;

    wl_list_remove(&relay->input_method_commit.link);
    wl_list_remove(&relay->input_method_grab_keyboard.link);
    wl_list_remove(&relay->input_method_destroy.link);
    wl_list_remove(&relay->new_popup_surface.link);

    struct text_input *text_input = relay_get_focused_text_input(relay);
    if (text_input) {
        // keyboard focus is still there, so keep the surface at hand in case
        // the input method returns
        struct wlr_surface *focused_surface = text_input_focused_surface(text_input);
        text_input_set_pending_focused_surface(text_input, focused_surface);
        text_input_send_leave(text_input);
    }
}

static void handle_input_surface_map(struct wl_listener *listener, void *data)
{
    struct input_popup *popup = wl_container_of(listener, popup, surface_map);
    input_popup_update(popup, popup->relay->seat);
    ky_scene_node_set_enabled(popup->surface_node, true);
}

static void handle_input_surface_unmap(struct wl_listener *listener, void *data)
{
    struct input_popup *popup = wl_container_of(listener, popup, surface_unmap);
    ky_scene_node_set_enabled(popup->surface_node, false);
}

static void handle_input_surface_commit(struct wl_listener *listener, void *data)
{
    struct input_popup *popup = wl_container_of(listener, popup, surface_commit);
    input_popup_update(popup, popup->relay->seat);
}

static void handle_input_popup_destroy(struct wl_listener *listener, void *data)
{
    struct input_popup *popup = wl_container_of(listener, popup, popup_surface_destroy);

    wl_list_remove(&popup->popup_surface_destroy.link);
    wl_list_remove(&popup->surface_commit.link);
    wl_list_remove(&popup->surface_unmap.link);
    wl_list_remove(&popup->surface_map.link);
    wl_list_remove(&popup->link);

    /* surface destroy signal is bebind this, destroy scene here */
    ky_scene_node_destroy(popup->surface_node);
    free(popup);
}

static bool input_popup_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                              uint32_t time, bool first, bool hold, void *data)
{
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    seat_notify_motion(seat, surface, time, x, y, first);
    return false;
}

static void input_popup_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                              bool pressed, uint32_t time, enum click_state state, void *data)
{
    seat_notify_button(seat, time, button, pressed);
}

static void input_popup_leave(struct seat *seat, struct ky_scene_node *node, bool last, void *data)
{
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    seat_notify_leave(seat, surface);
}

static const struct input_event_node_impl input_popup_event_node_impl = {
    .hover = input_popup_hover,
    .click = input_popup_click,
    .leave = input_popup_leave,
};

static struct ky_scene_node *input_popup_get_root(void *data)
{
    struct input_popup *popup = data;
    return popup->surface_node;
}

static void handle_new_popup_surface(struct wl_listener *listener, void *data)
{
    struct input_method_relay *relay = wl_container_of(listener, relay, new_popup_surface);
    struct wlr_input_popup_surface_v2 *popup_surface = data;

    struct input_popup *popup = calloc(1, sizeof(struct input_popup));
    if (!popup) {
        return;
    }

    popup->relay = relay;
    popup->popup_surface = popup_surface;
    wl_list_insert(&relay->input_popups, &popup->link);

    /* create a scene node to show the popup */
    struct view_layer *layer = view_manager_get_layer(LAYER_POPUP, false);
    struct ky_scene_surface *scene_surface =
        ky_scene_surface_create(layer->tree, popup_surface->surface);
    popup->surface_node = &scene_surface->buffer->node;
    input_event_node_create(popup->surface_node, &input_popup_event_node_impl, input_popup_get_root,
                            NULL, popup);
    ky_scene_node_set_enabled(popup->surface_node, popup_surface->surface->mapped);

    popup->surface_map.notify = handle_input_surface_map;
    wl_signal_add(&popup->popup_surface->surface->events.map, &popup->surface_map);
    popup->surface_unmap.notify = handle_input_surface_unmap;
    wl_signal_add(&popup->popup_surface->surface->events.unmap, &popup->surface_unmap);
    popup->surface_commit.notify = handle_input_surface_commit;
    wl_signal_add(&popup->popup_surface->surface->events.commit, &popup->surface_commit);
    popup->popup_surface_destroy.notify = handle_input_popup_destroy;
    wl_signal_add(&popup->popup_surface->events.destroy, &popup->popup_surface_destroy);
}

static void handle_new_input_method(struct wl_listener *listener, void *data)
{
    struct input_method_relay *relay = wl_container_of(listener, relay, new_input_method);
    struct wlr_input_method_v2 *input_method = data;
    if (relay->seat->wlr_seat != input_method->seat) {
        return;
    }

    if (relay->wlr_input_method) {
        kywc_log(KYWC_WARN, "Attempted to connect second input method to a seat");
        wlr_input_method_v2_send_unavailable(input_method);
        return;
    }

    relay->wlr_input_method = input_method;

    relay->input_method_commit.notify = handle_input_method_commit;
    wl_signal_add(&relay->wlr_input_method->events.commit, &relay->input_method_commit);
    relay->input_method_grab_keyboard.notify = handle_input_method_grab_keyboard;
    wl_signal_add(&relay->wlr_input_method->events.grab_keyboard,
                  &relay->input_method_grab_keyboard);
    relay->input_method_destroy.notify = handle_input_method_destroy;
    wl_signal_add(&relay->wlr_input_method->events.destroy, &relay->input_method_destroy);

    relay->new_popup_surface.notify = handle_new_popup_surface;
    wl_signal_add(&relay->wlr_input_method->events.new_popup_surface, &relay->new_popup_surface);

    /* if has a pending focused text_inputs */
    struct text_input *text_input;
    wl_list_for_each(text_input, &relay->text_inputs, link) {
        if (!text_input->pending_focused_surface) {
            continue;
        }
        text_input_send_enter(text_input, text_input->pending_focused_surface);
        text_input_set_pending_focused_surface(text_input, NULL);
        break;
    }

    if (relay->pending_enabled_text_input) {
        wlr_input_method_v2_send_activate(input_method);
        relay_send_input_method_state(relay, relay->pending_enabled_text_input);
        relay->pending_enabled_text_input = NULL;
    }
}

static void handle_input_method_v2_destroy(struct wl_listener *listener, void *data)
{
    struct input_method_relay *relay = wl_container_of(listener, relay, input_method_v2_destroy);
    wl_list_remove(&relay->input_method_v2_destroy.link);
    wl_list_remove(&relay->new_input_method.link);
    wl_list_init(&relay->input_method_v2_destroy.link);
    wl_list_init(&relay->new_input_method.link);
}

static void handle_text_input_v2_destroy(struct wl_listener *listener, void *data)
{
    struct input_method_relay *relay = wl_container_of(listener, relay, text_input_v2_destroy);
    wl_list_remove(&relay->text_input_v2_destroy.link);
    wl_list_remove(&relay->new_text_input_v2.link);
    wl_list_init(&relay->text_input_v2_destroy.link);
    wl_list_init(&relay->new_text_input_v2.link);
}

static void handle_text_input_v3_destroy(struct wl_listener *listener, void *data)
{
    struct input_method_relay *relay = wl_container_of(listener, relay, text_input_v3_destroy);
    wl_list_remove(&relay->text_input_v3_destroy.link);
    wl_list_remove(&relay->new_text_input_v3.link);
    wl_list_init(&relay->text_input_v3_destroy.link);
    wl_list_init(&relay->new_text_input_v3.link);
}

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct input_method_relay *relay = wl_container_of(listener, relay, seat_destroy);
    wl_list_remove(&relay->seat_destroy.link);

    handle_input_method_v2_destroy(&relay->input_method_v2_destroy, NULL);
    handle_text_input_v2_destroy(&relay->text_input_v2_destroy, NULL);
    handle_text_input_v3_destroy(&relay->text_input_v3_destroy, NULL);

    if (relay->wlr_input_method) {
        handle_input_method_destroy(&relay->input_method_destroy, relay->wlr_input_method);
    }

    struct text_input *input, *tmp_input;
    wl_list_for_each_safe(input, tmp_input, &relay->text_inputs, link) {
        wl_list_remove(&input->link);
        wl_list_init(&input->link);
    }

    struct input_popup *popup, *tmp_popup;
    wl_list_for_each_safe(popup, tmp_popup, &relay->input_popups, link) {
        wl_list_remove(&popup->link);
        wl_list_init(&popup->link);
    }

    free(relay);
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct input_method_manager *manager = wl_container_of(listener, manager, new_seat);
    struct seat *seat = data;

    /* create input_manager for this seat */
    struct input_method_relay *relay = calloc(1, sizeof(struct input_method_relay));
    if (!relay) {
        return;
    }

    wl_list_init(&relay->text_inputs);
    wl_list_init(&relay->input_popups);
    seat->relay = relay;

    relay->seat = seat;
    relay->seat_destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &relay->seat_destroy);

    relay->new_input_method.notify = handle_new_input_method;
    wl_signal_add(&manager->input_method->events.input_method, &relay->new_input_method);
    relay->input_method_v2_destroy.notify = handle_input_method_v2_destroy;
    wl_signal_add(&manager->input_method->events.destroy, &relay->input_method_v2_destroy);

    relay->new_text_input_v2.notify = handle_new_text_input_v2;
    wl_signal_add(&manager->text_input_v2->events.text_input, &relay->new_text_input_v2);
    relay->text_input_v2_destroy.notify = handle_text_input_v2_destroy;
    wl_signal_add(&manager->text_input_v2->events.destroy, &relay->text_input_v2_destroy);

    relay->new_text_input_v3.notify = handle_new_text_input_v3;
    wl_signal_add(&manager->text_input_v3->events.text_input, &relay->new_text_input_v3);
    relay->text_input_v3_destroy.notify = handle_text_input_v3_destroy;
    wl_signal_add(&manager->text_input_v3->events.destroy, &relay->text_input_v3_destroy);
}

static void handle_text_input_v1_destroy(struct wl_listener *listener, void *data)
{
    struct input_method_manager *manager =
        wl_container_of(listener, manager, text_input_v1_destroy);
    wl_list_remove(&manager->text_input_v1_destroy.link);
    wl_list_remove(&manager->new_text_input_v1.link);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct input_method_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    wl_list_remove(&manager->new_seat.link);
    free(manager);
}

bool input_method_manager_create(struct input_manager *input_manager)
{
    struct input_method_manager *manager = calloc(1, sizeof(struct input_method_manager));
    if (!manager) {
        return false;
    }

    manager->input_method = wlr_input_method_manager_v2_create(input_manager->server->display);
    manager->text_input_v3 = wlr_text_input_manager_v3_create(input_manager->server->display);
    manager->text_input_v2 = text_input_manager_v2_create(input_manager->server->display);

    /* no seat in text_input_v1 create_text_input request */
    manager->text_input_v1 = text_input_manager_v1_create(input_manager->server->display);
    manager->new_text_input_v1.notify = handle_new_text_input_v1;
    wl_signal_add(&manager->text_input_v1->events.text_input, &manager->new_text_input_v1);
    manager->text_input_v1_destroy.notify = handle_text_input_v1_destroy;
    wl_signal_add(&manager->text_input_v1->events.destroy, &manager->text_input_v1_destroy);

    manager->new_seat.notify = handle_new_seat;
    wl_signal_add(&input_manager->events.new_seat, &manager->new_seat);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(input_manager->server, &manager->server_destroy);

    return true;
}

static bool text_input_match_surface(struct text_input *text_input, struct wlr_surface *surface)
{
    if (text_input->text_input_v3) {
        return wl_resource_get_client(text_input->text_input_v3->resource) ==
               wl_resource_get_client(surface->resource);
    } else if (text_input->text_input_v2) {
        return wl_resource_get_client(text_input->text_input_v2->resource) ==
               wl_resource_get_client(surface->resource);
    } else {
        return wl_resource_get_client(text_input->text_input_v1->resource) ==
                   wl_resource_get_client(surface->resource) &&
               text_input->text_input_v1->surface == surface;
    }
}

void input_method_set_focus(struct seat *seat, struct wlr_surface *surface)
{
    struct input_method_relay *relay = seat->relay;
    struct wlr_surface *focused_surface;

    struct text_input *text_input;
    wl_list_for_each(text_input, &relay->text_inputs, link) {
        focused_surface = text_input_focused_surface(text_input);
        if (text_input->pending_focused_surface) {
            if (surface != text_input->pending_focused_surface) {
                text_input_set_pending_focused_surface(text_input, NULL);
            }
        } else if (focused_surface) {
            if (surface != focused_surface) {
                relay_disable_text_input(relay, text_input);
                text_input_send_leave(text_input);
            } else {
                kywc_log(KYWC_DEBUG, "IM relay set_focus already focused");
                continue;
            }
        }

        if (surface && text_input_match_surface(text_input, surface)) {
            if (relay->wlr_input_method) {
                text_input_send_enter(text_input, surface);
            } else {
                text_input_set_pending_focused_surface(text_input, surface);
            }
        }
    }
}

static struct wlr_input_method_keyboard_grab_v2 *
keyboard_get_input_method_grab(struct keyboard *keyboard)
{
    struct wlr_input_method_v2 *input_method = keyboard->seat->relay->wlr_input_method;
    struct wlr_virtual_keyboard_v1 *virtual_keyboard =
        wlr_input_device_get_virtual_keyboard(&keyboard->wlr_keyboard->base);
    if (!input_method || !input_method->keyboard_grab ||
        (virtual_keyboard && wl_resource_get_client(virtual_keyboard->resource) ==
                                 wl_resource_get_client(input_method->keyboard_grab->resource))) {
        return NULL;
    }
    return input_method->keyboard_grab;
}

bool input_method_handle_key(struct keyboard *keyboard, uint32_t time, uint32_t key, uint32_t state)
{
    struct wlr_input_method_keyboard_grab_v2 *keyboard_grab =
        keyboard_get_input_method_grab(keyboard);
    if (!keyboard_grab) {
        return false;
    }

    wlr_input_method_keyboard_grab_v2_set_keyboard(keyboard_grab, keyboard->wlr_keyboard);
    wlr_input_method_keyboard_grab_v2_send_key(keyboard_grab, time, key, state);
    return true;
}

bool input_method_handle_modifiers(struct keyboard *keyboard)
{
    struct wlr_input_method_keyboard_grab_v2 *keyboard_grab =
        keyboard_get_input_method_grab(keyboard);
    if (!keyboard_grab) {
        return false;
    }

    wlr_input_method_keyboard_grab_v2_set_keyboard(keyboard_grab, keyboard->wlr_keyboard);
    wlr_input_method_keyboard_grab_v2_send_modifiers(keyboard_grab,
                                                     &keyboard->wlr_keyboard->modifiers);
    return true;
}

bool keyboard_is_from_input_method(struct keyboard *keyboard)
{
    struct wlr_input_method_v2 *input_method = keyboard->seat->relay->wlr_input_method;
    struct wlr_virtual_keyboard_v1 *virtual_keyboard =
        wlr_input_device_get_virtual_keyboard(&keyboard->wlr_keyboard->base);

    return input_method && virtual_keyboard &&
           wl_resource_get_client(virtual_keyboard->resource) ==
               wl_resource_get_client(input_method->resource);
}
