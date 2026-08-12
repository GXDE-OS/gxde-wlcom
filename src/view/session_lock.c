// SPDX-FileCopyrightText: 2026 CharOfString <root@charofstring.cc>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_session_lock_v1.h>

#include <kywc/log.h>

#include "input/event.h"
#include "input/input.h"
#include "input/seat.h"
#include "output.h"
#include "scene/surface.h"
#include "server.h"
#include "view/session_lock.h"
#include "view/view.h"

struct session_lock;

struct session_lock_output {
    struct wl_list link;
    struct session_lock *lock;
    struct output *output;

    struct ky_scene_tree *tree;
    struct ky_scene_rect *background;
    struct ky_scene_tree *surface_tree;
    struct wlr_session_lock_surface_v1 *surface;
    uint32_t previous_commit_seq;
    bool presented;

    struct wl_listener output_geometry;
    struct wl_listener output_disable;
    struct wl_listener output_present;
    struct wl_listener surface_map;
    struct wl_listener surface_destroy;
};

struct session_lock {
    struct wlr_session_lock_v1 *protocol_lock;
    struct wl_list outputs;
    struct wlr_surface *focused;
    bool abandoned;
    bool locked_sent;
    bool destroying;

    struct wl_listener new_surface;
    struct wl_listener unlock;
    struct wl_listener destroy;
};

struct session_lock_manager {
    struct wlr_session_lock_manager_v1 *protocol_manager;
    struct session_lock *lock;

    struct wl_listener new_lock;
    struct wl_listener new_output;
    struct wl_listener protocol_destroy;
    struct wl_listener server_destroy;
};

static struct session_lock_manager *manager = NULL;

static void session_lock_try_send_locked(struct session_lock *lock);

static void listener_remove(struct wl_listener *listener)
{
    if (!wl_list_empty(&listener->link)) {
        wl_list_remove(&listener->link);
        wl_list_init(&listener->link);
    }
}

static void listener_init(struct wl_listener *listener)
{
    wl_list_init(&listener->link);
}

bool session_lock_is_active(void)
{
    return manager && manager->lock;
}

bool session_lock_surface_is_allowed(struct wlr_surface *surface)
{
    if (!session_lock_is_active() || !surface) {
        return false;
    }

    struct wlr_surface *root = wlr_surface_get_root_surface(surface);
    struct session_lock_output *lock_output;
    wl_list_for_each(lock_output, &manager->lock->outputs, link) {
        if (lock_output->surface && lock_output->surface->surface == root) {
            return true;
        }
    }

    return false;
}

struct wlr_surface *session_lock_get_focus_surface(void)
{
    if (!session_lock_is_active()) {
        return NULL;
    }

    struct session_lock *lock = manager->lock;
    if (lock->focused && lock->focused->mapped) {
        return lock->focused;
    }

    struct session_lock_output *lock_output;
    wl_list_for_each(lock_output, &lock->outputs, link) {
        if (lock_output->surface && lock_output->surface->surface->mapped) {
            return lock_output->surface->surface;
        }
    }

    return NULL;
}

static struct session_lock_output *lock_output_from_wlr_output(struct session_lock *lock,
                                                               struct wlr_output *wlr_output)
{
    struct session_lock_output *lock_output;
    wl_list_for_each(lock_output, &lock->outputs, link) {
        if (lock_output->output->wlr_output == wlr_output) {
            return lock_output;
        }
    }

    return NULL;
}

static bool focus_seat(struct seat *seat, int index, void *data)
{
    (void)index;
    seat_focus_surface(seat, data);
    return false;
}

static void focus_surface(struct session_lock *lock, struct wlr_surface *surface)
{
    lock->focused = surface;
    input_manager_for_each_seat(focus_seat, surface);
}

static void refocus_surface(struct session_lock *lock, struct wlr_surface *removed)
{
    if (lock->focused != removed) {
        return;
    }

    lock->focused = NULL;
    focus_surface(lock, session_lock_get_focus_surface());
}

static bool lock_surface_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                               uint32_t time, bool first, bool hold, void *data)
{
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    if (!session_lock_surface_is_allowed(surface)) {
        if (first) {
            seat_notify_leave(seat, NULL);
        }
        return false;
    }

    seat_notify_motion(seat, surface, time, x, y, first);
    return false;
}

static void lock_surface_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                               bool pressed, uint32_t time, enum click_state state, void *data)
{
    struct session_lock_output *lock_output = data;
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    if (session_lock_surface_is_allowed(surface)) {
        seat_notify_button(seat, time, button, pressed);
    }

    if (lock_output->surface && lock_output->surface->surface->mapped) {
        focus_surface(lock_output->lock, lock_output->surface->surface);
    }
}

static void lock_surface_leave(struct seat *seat, struct ky_scene_node *node, bool last, void *data)
{
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    seat_notify_leave(seat, surface);
}

static struct ky_scene_node *lock_surface_get_root(void *data)
{
    struct session_lock_output *lock_output = data;
    return &lock_output->tree->node;
}

static struct wlr_surface *lock_surface_get_toplevel(void *data)
{
    struct session_lock_output *lock_output = data;
    return lock_output->surface ? lock_output->surface->surface : NULL;
}

static const struct input_event_node_impl lock_surface_event_node_impl = {
    .hover = lock_surface_hover,
    .click = lock_surface_click,
    .leave = lock_surface_leave,
};

static void lock_output_configure(struct session_lock_output *lock_output)
{
    struct kywc_box *geometry = &lock_output->output->geometry;
    ky_scene_node_set_position(&lock_output->tree->node, geometry->x, geometry->y);
    ky_scene_rect_set_size(lock_output->background, geometry->width, geometry->height);

    if (lock_output->surface) {
        wlr_session_lock_surface_v1_configure(lock_output->surface, geometry->width,
                                              geometry->height);
    }
}

static void session_lock_try_send_locked(struct session_lock *lock)
{
    if (lock->destroying || lock->locked_sent || !lock->protocol_lock) {
        return;
    }

    struct session_lock_output *lock_output;
    wl_list_for_each(lock_output, &lock->outputs, link) {
        if (lock_output->output->base.state.enabled && !lock_output->presented) {
            return;
        }
    }

    lock->locked_sent = true;
    wlr_session_lock_v1_send_locked(lock->protocol_lock);
    kywc_log(KYWC_INFO, "session locked");
}

static void handle_output_geometry(struct wl_listener *listener, void *data)
{
    struct session_lock_output *lock_output =
        wl_container_of(listener, lock_output, output_geometry);
    lock_output_configure(lock_output);
}

static void handle_output_present(struct wl_listener *listener, void *data)
{
    struct session_lock_output *lock_output =
        wl_container_of(listener, lock_output, output_present);
    struct wlr_output_event_present *event = data;

    if (lock_output->presented || !lock_output->output->base.state.enabled ||
        event->commit_seq == lock_output->previous_commit_seq) {
        return;
    }
    if (!event->presented) {
        output_schedule_frame(lock_output->output->wlr_output);
        return;
    }

    lock_output->presented = true;
    session_lock_try_send_locked(lock_output->lock);
}

static void lock_output_destroy(struct session_lock_output *lock_output)
{
    struct session_lock *lock = lock_output->lock;
    struct wlr_surface *surface = lock_output->surface ? lock_output->surface->surface : NULL;
    lock_output->surface = NULL;
    refocus_surface(lock_output->lock, surface);

    listener_remove(&lock_output->output_geometry);
    listener_remove(&lock_output->output_disable);
    listener_remove(&lock_output->output_present);
    listener_remove(&lock_output->surface_map);
    listener_remove(&lock_output->surface_destroy);
    wl_list_remove(&lock_output->link);

    ky_scene_node_destroy(&lock_output->tree->node);
    free(lock_output);
    session_lock_try_send_locked(lock);
}

static void handle_output_disable(struct wl_listener *listener, void *data)
{
    struct session_lock_output *lock_output =
        wl_container_of(listener, lock_output, output_disable);
    if (lock_output->output->base.destroying) {
        lock_output_destroy(lock_output);
        return;
    }

    ky_scene_node_set_enabled(&lock_output->tree->node, false);
    session_lock_try_send_locked(lock_output->lock);
}

static struct session_lock_output *lock_output_create(struct session_lock *lock,
                                                      struct output *output)
{
    struct session_lock_output *lock_output = calloc(1, sizeof(*lock_output));
    if (!lock_output) {
        return NULL;
    }

    lock_output->lock = lock;
    lock_output->output = output;
    listener_init(&lock_output->output_geometry);
    listener_init(&lock_output->output_disable);
    listener_init(&lock_output->output_present);
    listener_init(&lock_output->surface_map);
    listener_init(&lock_output->surface_destroy);

    struct view_layer *layer = view_manager_get_layer(LAYER_SCREEN_LOCK, false);
    lock_output->tree = ky_scene_tree_create(layer->tree);
    if (!lock_output->tree) {
        free(lock_output);
        return NULL;
    }

    const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    lock_output->background = ky_scene_rect_create(lock_output->tree, 0, 0, black);
    if (!lock_output->background ||
        !input_event_node_create(&lock_output->tree->node, &lock_surface_event_node_impl,
                                 lock_surface_get_root, lock_surface_get_toplevel, lock_output)) {
        ky_scene_node_destroy(&lock_output->tree->node);
        free(lock_output);
        return NULL;
    }

    wl_list_insert(&lock->outputs, &lock_output->link);

    lock_output->output_geometry.notify = handle_output_geometry;
    wl_signal_add(&output->events.geometry, &lock_output->output_geometry);
    lock_output->output_disable.notify = handle_output_disable;
    wl_signal_add(&output->events.disable, &lock_output->output_disable);
    lock_output->output_present.notify = handle_output_present;
    wl_signal_add(&output->wlr_output->events.present, &lock_output->output_present);

    lock_output->previous_commit_seq = output->wlr_output->commit_seq;
    lock_output_configure(lock_output);
    ky_scene_node_set_enabled(&lock_output->tree->node, output->base.state.enabled);
    if (output->base.state.enabled) {
        output_schedule_frame(output->wlr_output);
    }
    return lock_output;
}

static void handle_surface_map(struct wl_listener *listener, void *data)
{
    struct session_lock_output *lock_output = wl_container_of(listener, lock_output, surface_map);
    if (!lock_output->lock->focused) {
        focus_surface(lock_output->lock, lock_output->surface->surface);
    }
}

static void handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct session_lock_output *lock_output =
        wl_container_of(listener, lock_output, surface_destroy);
    struct wlr_surface *surface = lock_output->surface->surface;

    listener_remove(&lock_output->surface_map);
    listener_remove(&lock_output->surface_destroy);
    if (lock_output->surface_tree) {
        ky_scene_node_destroy(&lock_output->surface_tree->node);
        lock_output->surface_tree = NULL;
    }
    lock_output->surface = NULL;
    refocus_surface(lock_output->lock, surface);
}

static void handle_new_surface(struct wl_listener *listener, void *data)
{
    struct session_lock *lock = wl_container_of(listener, lock, new_surface);
    struct wlr_session_lock_surface_v1 *surface = data;
    struct session_lock_output *lock_output = lock_output_from_wlr_output(lock, surface->output);

    if (!lock_output) {
        struct output *output = output_from_wlr_output(surface->output);
        lock_output = lock_output_create(lock, output);
    }
    if (!lock_output || lock_output->surface) {
        kywc_log(KYWC_FATAL, "failed to attach session lock surface");
        abort();
    }

    lock_output->surface = surface;
    lock_output->surface_tree =
        ky_scene_subsurface_tree_create(lock_output->tree, surface->surface);
    if (!lock_output->surface_tree) {
        kywc_log(KYWC_FATAL, "failed to create session lock surface scene");
        abort();
    }

    lock_output->surface_map.notify = handle_surface_map;
    wl_signal_add(&surface->surface->events.map, &lock_output->surface_map);
    lock_output->surface_destroy.notify = handle_surface_destroy;
    wl_signal_add(&surface->events.destroy, &lock_output->surface_destroy);
    lock_output_configure(lock_output);
}

static void session_lock_detach_protocol(struct session_lock *lock)
{
    listener_remove(&lock->new_surface);
    listener_remove(&lock->unlock);
    listener_remove(&lock->destroy);
}

static bool cancel_seat_grabs(struct seat *seat, int index, void *data)
{
    (void)index;
    (void)data;
    seat_cancel_grabs(seat);
    return false;
}

static void session_lock_destroy(struct session_lock *lock, bool restore_focus)
{
    lock->destroying = true;
    session_lock_detach_protocol(lock);

    if (restore_focus) {
        input_manager_for_each_seat(cancel_seat_grabs, NULL);
    }

    struct session_lock_output *lock_output, *tmp;
    wl_list_for_each_safe(lock_output, tmp, &lock->outputs, link) {
        lock_output_destroy(lock_output);
    }

    if (manager && manager->lock == lock) {
        manager->lock = NULL;
    }
    free(lock);

    if (restore_focus) {
        view_activate_topmost();
    }
}

static void handle_unlock(struct wl_listener *listener, void *data)
{
    struct session_lock *lock = wl_container_of(listener, lock, unlock);
    kywc_log(KYWC_INFO, "session unlocked");
    session_lock_destroy(lock, true);
}

static void handle_lock_abandoned(struct wl_listener *listener, void *data)
{
    struct session_lock *lock = wl_container_of(listener, lock, destroy);
    if (!lock->locked_sent) {
        kywc_log(KYWC_WARN, "session lock client disappeared before locking completed");
        session_lock_destroy(lock, true);
        return;
    }

    kywc_log(KYWC_ERROR, "session lock client disappeared; keeping the session locked");

    session_lock_detach_protocol(lock);
    lock->protocol_lock = NULL;
    lock->abandoned = true;
    focus_surface(lock, NULL);
}

struct add_outputs_context {
    struct session_lock *lock;
    bool failed;
};

static bool add_lock_output(struct kywc_output *kywc_output, int index, void *data)
{
    struct add_outputs_context *context = data;
    struct output *output = output_from_kywc_output(kywc_output);
    if (!lock_output_create(context->lock, output)) {
        context->failed = true;
        return true;
    }
    return false;
}

static void handle_new_lock(struct wl_listener *listener, void *data)
{
    struct wlr_session_lock_v1 *protocol_lock = data;

    if (manager->lock) {
        if (!manager->lock->abandoned) {
            kywc_log(KYWC_WARN, "refusing a second session lock");
            wlr_session_lock_v1_destroy(protocol_lock);
            return;
        }
        session_lock_destroy(manager->lock, false);
    }

    struct session_lock *lock = calloc(1, sizeof(*lock));
    if (!lock) {
        wlr_session_lock_v1_destroy(protocol_lock);
        return;
    }

    lock->protocol_lock = protocol_lock;
    wl_list_init(&lock->outputs);
    listener_init(&lock->new_surface);
    listener_init(&lock->unlock);
    listener_init(&lock->destroy);

    lock->new_surface.notify = handle_new_surface;
    wl_signal_add(&protocol_lock->events.new_surface, &lock->new_surface);
    lock->unlock.notify = handle_unlock;
    wl_signal_add(&protocol_lock->events.unlock, &lock->unlock);
    lock->destroy.notify = handle_lock_abandoned;
    wl_signal_add(&protocol_lock->events.destroy, &lock->destroy);

    manager->lock = lock;

    struct add_outputs_context context = { .lock = lock };
    output_manager_for_each_output(add_lock_output, true, &context);
    if (context.failed) {
        session_lock_destroy(lock, false);
        wlr_session_lock_v1_destroy(protocol_lock);
        return;
    }

    input_manager_for_each_seat(cancel_seat_grabs, NULL);
    focus_surface(lock, NULL);
    session_lock_try_send_locked(lock);
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    if (!manager->lock) {
        return;
    }

    struct output *output = output_from_kywc_output(data);
    struct session_lock_output *lock_output =
        lock_output_from_wlr_output(manager->lock, output->wlr_output);
    if (!lock_output) {
        lock_output = lock_output_create(manager->lock, output);
        if (!lock_output) {
            kywc_log(KYWC_FATAL, "failed to secure a newly enabled output");
            abort();
        }
    }

    lock_output->presented = false;
    lock_output->previous_commit_seq = output->wlr_output->commit_seq;
    lock_output_configure(lock_output);
    ky_scene_node_set_enabled(&lock_output->tree->node, true);
    output_schedule_frame(output->wlr_output);
}

static void session_lock_manager_destroy(void)
{
    if (!manager) {
        return;
    }

    if (manager->lock) {
        session_lock_destroy(manager->lock, false);
    }

    listener_remove(&manager->new_lock);
    listener_remove(&manager->new_output);
    listener_remove(&manager->protocol_destroy);
    listener_remove(&manager->server_destroy);
    free(manager);
    manager = NULL;
}

static void handle_protocol_manager_destroy(struct wl_listener *listener, void *data)
{
    session_lock_manager_destroy();
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    session_lock_manager_destroy();
}

bool session_lock_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    listener_init(&manager->new_lock);
    listener_init(&manager->new_output);
    listener_init(&manager->protocol_destroy);
    listener_init(&manager->server_destroy);

    manager->protocol_manager = wlr_session_lock_manager_v1_create(server->display);
    if (!manager->protocol_manager) {
        free(manager);
        manager = NULL;
        return false;
    }

    manager->new_lock.notify = handle_new_lock;
    wl_signal_add(&manager->protocol_manager->events.new_lock, &manager->new_lock);
    manager->protocol_destroy.notify = handle_protocol_manager_destroy;
    wl_signal_add(&manager->protocol_manager->events.destroy, &manager->protocol_destroy);

    manager->new_output.notify = handle_new_output;
    output_manager_add_new_enabled_listener(&manager->new_output);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    return true;
}
