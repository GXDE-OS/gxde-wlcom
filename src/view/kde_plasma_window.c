// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <kywc/identifier.h>

#include "input/seat.h"
#include "plasma-window-management-protocol.h"
#include "view_p.h"

#define PLASMA_WINDOW_MANAGEMENT_VERSION 16

struct kde_plasma_window_management {
    struct wl_global *global;
    struct wl_list resources;
    struct wl_list windows;

    struct wl_listener new_view;
    struct wl_listener show_desktop;
    struct wl_listener display_destroy;
    struct wl_listener server_destroy;

    uint32_t window_id_counter;
};

struct kde_plasma_window {
    struct wl_list resources;
    struct wl_list link;
    struct kde_plasma_window_management *management;

    struct kywc_view *kywc_view;
    struct wl_listener view_map;
    struct wl_listener view_unmap;
    struct wl_listener view_destroy;
    struct wl_listener view_title;
    struct wl_listener view_app_id;
    struct wl_listener view_activate;
    struct wl_listener view_minimize;
    struct wl_listener view_maximize;
    struct wl_listener view_fullscreen;
    struct wl_listener view_capabilities;

    /* The internal window id and uuid */
    uint32_t id;
    const char *uuid;
    /* bitfield of state flags */
    uint32_t states;
};

enum state_flag {
    STATE_FLAG_ACTIVE = 0,
    STATE_FLAG_MINIMIZED,
    STATE_FLAG_MAXIMIZED,
    STATE_FLAG_FULLSCREEN,
    STATE_FLAG_KEEP_ABOVE,
    STATE_FLAG_KEEP_BELOW,
    STATE_FLAG_ON_ALL_DESKTOPS,
    STATE_FLAG_DEMANDS_ATTENTION,
    STATE_FLAG_CLOSEABLE,
    STATE_FLAG_MINIMIZABLE,
    STATE_FLAG_MAXIMIZABLE,
    STATE_FLAG_FULLSCREENABLE,
    STATE_FLAG_SKIPTASKBAR,
    STATE_FLAG_SHADEABLE,
    STATE_FLAG_SHADED,
    STATE_FLAG_MOVABLE,
    STATE_FLAG_RESIZABLE,
    STATE_FLAG_VIRTUAL_DESKTOP_CHANGEABLE,
    STATE_FLAG_SKIPSWITCHER,
    STATE_FLAG_LAST,
};

static void kde_plasma_window_set_state(struct kde_plasma_window *window, enum state_flag flag,
                                        bool state)
{
    switch (flag) {
    case STATE_FLAG_ACTIVE:
        assert(state);
        kywc_view_activate(window->kywc_view);
        seat_focus_surface(input_manager_get_default_seat(),
                           view_from_kywc_view(window->kywc_view)->surface);
        break;
    case STATE_FLAG_MINIMIZED:
        kywc_view_set_minimized(window->kywc_view, state);
        break;
    case STATE_FLAG_MAXIMIZED:
        kywc_view_set_maximized(window->kywc_view, state, NULL);
        break;
    case STATE_FLAG_FULLSCREEN:
        kywc_view_set_fullscreen(window->kywc_view, state, NULL);
        break;
    case STATE_FLAG_KEEP_ABOVE:
        kywc_view_set_kept_above(window->kywc_view, state);
        break;
    case STATE_FLAG_KEEP_BELOW:
        kywc_view_set_kept_below(window->kywc_view, state);
        break;
    case STATE_FLAG_ON_ALL_DESKTOPS:
        break;
    case STATE_FLAG_DEMANDS_ATTENTION:
        break;
    case STATE_FLAG_CLOSEABLE:
        window->kywc_view->closeable = state;
        break;
    case STATE_FLAG_MINIMIZABLE:
        window->kywc_view->minimizable = state;
        break;
    case STATE_FLAG_MAXIMIZABLE:
        window->kywc_view->maximizable = state;
        break;
    case STATE_FLAG_FULLSCREENABLE:
        window->kywc_view->fullscreenable = state;
        break;
    case STATE_FLAG_SKIPTASKBAR:
        window->kywc_view->skip_taskbar = state;
        break;
    case STATE_FLAG_SHADEABLE:
        break;
    case STATE_FLAG_SHADED:
        break;
    case STATE_FLAG_MOVABLE:
        window->kywc_view->movable = state;
        break;
    case STATE_FLAG_RESIZABLE:
        window->kywc_view->resizable = state;
        break;
    case STATE_FLAG_VIRTUAL_DESKTOP_CHANGEABLE:
        break;
    case STATE_FLAG_SKIPSWITCHER:
        window->kywc_view->skip_switcher = state;
        break;
    case STATE_FLAG_LAST:
        break;
    }
}

static void kde_plasma_window_send_state(struct kde_plasma_window *window,
                                         struct wl_resource *resource, bool force);

static void handle_set_state(struct wl_client *client, struct wl_resource *resource, uint32_t flags,
                             uint32_t state)
{
    struct kde_plasma_window *window = wl_resource_get_user_data(resource);
    if (!window) {
        return;
    }
    for (int i = 0; i < STATE_FLAG_LAST; i++) {
        if ((flags >> i) & 0x1) {
            kde_plasma_window_set_state(window, i, (state >> i) & 0x1);
        }
    }
    kde_plasma_window_send_state(window, NULL, false);
}

static void handle_set_virtual_desktop(struct wl_client *client, struct wl_resource *resource,
                                       uint32_t number)
{
    // Not implemented yet
}

static void handle_set_minimized_geometry(struct wl_client *client, struct wl_resource *resource,
                                          struct wl_resource *panel, uint32_t x, uint32_t y,
                                          uint32_t width, uint32_t height)
{
    // Not implemented yet
}

static void handle_unset_minimized_geometry(struct wl_client *client, struct wl_resource *resource,
                                            struct wl_resource *panel)
{
    // Not implemented yet
}

static void handle_close(struct wl_client *client, struct wl_resource *resource)
{
    struct kde_plasma_window *window = wl_resource_get_user_data(resource);
    if (!window) {
        return;
    }
    kywc_view_close(window->kywc_view);
}

static void handle_request_move(struct wl_client *client, struct wl_resource *resource)
{
    // Not implemented yet
}

static void handle_request_resize(struct wl_client *client, struct wl_resource *resource)
{
    // Not implemented yet
}

static void handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void handle_get_icon(struct wl_client *client, struct wl_resource *resource, int32_t fd)
{
    // Not implemented yet
}

static void handle_request_enter_virtual_desktop(struct wl_client *client,
                                                 struct wl_resource *resource, const char *id)
{
    // Not implemented yet
}

static void handle_request_enter_new_virtual_desktop(struct wl_client *client,
                                                     struct wl_resource *resource)
{
    // Not implemented yet
}

static void handle_request_leave_virtual_desktop(struct wl_client *client,
                                                 struct wl_resource *resource, const char *id)
{
    // Not implemented yet
}

static void handle_request_enter_activity(struct wl_client *client, struct wl_resource *resource,
                                          const char *id)
{
    // Not implemented yet
}

static void handle_request_leave_activity(struct wl_client *client, struct wl_resource *resource,
                                          const char *id)
{
    // Not implemented yet
}

static void handle_send_to_output(struct wl_client *client, struct wl_resource *resource,
                                  struct wl_resource *output)
{
    // Not implemented yet
}

static const struct org_kde_plasma_window_interface kde_plasma_window_impl = {
    .set_state = handle_set_state,
    .set_virtual_desktop = handle_set_virtual_desktop,
    .set_minimized_geometry = handle_set_minimized_geometry,
    .unset_minimized_geometry = handle_unset_minimized_geometry,
    .close = handle_close,
    .request_move = handle_request_move,
    .request_resize = handle_request_resize,
    .destroy = handle_destroy,
    .get_icon = handle_get_icon,
    .request_enter_virtual_desktop = handle_request_enter_virtual_desktop,
    .request_enter_new_virtual_desktop = handle_request_enter_new_virtual_desktop,
    .request_leave_virtual_desktop = handle_request_leave_virtual_desktop,
    .request_enter_activity = handle_request_enter_activity,
    .request_leave_activity = handle_request_leave_activity,
    .send_to_output = handle_send_to_output,
};

static struct kde_plasma_window *
kde_plasma_window_from_id(struct kde_plasma_window_management *management, uint32_t id)
{
    struct kde_plasma_window *window;
    wl_list_for_each(window, &management->windows, link) {
        if (window->id == id) {
            return window;
        }
    }
    return NULL;
}

static struct kde_plasma_window *
kde_plasma_window_from_uuid(struct kde_plasma_window_management *management, const char *uuid)
{
    struct kde_plasma_window *window;
    wl_list_for_each(window, &management->windows, link) {
        if (strcmp(window->uuid, uuid) == 0) {
            return window;
        }
    }
    return NULL;
}

static void window_handle_resource_destroy(struct wl_resource *resource)
{
    wl_resource_set_destructor(resource, NULL);
    wl_resource_set_user_data(resource, NULL);
    wl_list_remove(&resource->link);
}

static void window_handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct kde_plasma_window *window = wl_container_of(listener, window, view_unmap);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        org_kde_plasma_window_send_unmapped(resource);
    }
}

static void window_handle_view_title(struct wl_listener *listener, void *data)
{
    struct kde_plasma_window *window = wl_container_of(listener, window, view_title);
    if (!window->kywc_view->title) {
        return;
    }

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        org_kde_plasma_window_send_title_changed(resource, window->kywc_view->title);
    }
}

static void window_handle_view_app_id(struct wl_listener *listener, void *data)
{
    struct kde_plasma_window *window = wl_container_of(listener, window, view_app_id);
    if (!window->kywc_view->app_id) {
        return;
    }

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        org_kde_plasma_window_send_app_id_changed(resource, window->kywc_view->app_id);
    }
}

#define set_state(states, prop, state)                                                             \
    if (prop) {                                                                                    \
        states |= ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_##state;                                  \
    } else {                                                                                       \
        states &= ~ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_##state;                                 \
    }

static void window_handle_view_activate(struct wl_listener *listener, void *data)
{
    struct kde_plasma_window *window = wl_container_of(listener, window, view_activate);
    set_state(window->states, window->kywc_view->activated, ACTIVE);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        org_kde_plasma_window_send_state_changed(resource, window->states);
    }
}

static void window_handle_view_minimize(struct wl_listener *listener, void *data)
{
    struct kde_plasma_window *window = wl_container_of(listener, window, view_minimize);
    set_state(window->states, window->kywc_view->minimized, MINIMIZED);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        org_kde_plasma_window_send_state_changed(resource, window->states);
    }
}

static void window_handle_view_maximize(struct wl_listener *listener, void *data)
{
    struct kde_plasma_window *window = wl_container_of(listener, window, view_maximize);
    set_state(window->states, window->kywc_view->maximized, MAXIMIZED);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        org_kde_plasma_window_send_state_changed(resource, window->states);
    }
}

static void window_handle_view_fullscreen(struct wl_listener *listener, void *data)
{
    struct kde_plasma_window *window = wl_container_of(listener, window, view_fullscreen);
    set_state(window->states, window->kywc_view->fullscreen, FULLSCREEN);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        org_kde_plasma_window_send_state_changed(resource, window->states);
    }
}

static void window_handle_view_capabilities(struct wl_listener *listener, void *data)
{
    struct kde_plasma_window *window = wl_container_of(listener, window, view_capabilities);
    kde_plasma_window_send_state(window, NULL, false);
}

static void kde_plasma_window_send_state(struct kde_plasma_window *window,
                                         struct wl_resource *resource, bool force)
{
    struct kywc_view *kywc_view = window->kywc_view;
    uint32_t states = window->states;

    set_state(states, kywc_view->activated, ACTIVE);
    set_state(states, kywc_view->minimized, MINIMIZED);
    set_state(states, kywc_view->maximized, MAXIMIZED);
    set_state(states, kywc_view->fullscreen, FULLSCREEN);
    set_state(states, kywc_view->kept_above, KEEP_ABOVE);
    set_state(states, kywc_view->kept_below, KEEP_BELOW);
    // ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ON_ALL_DESKTOPS
    // ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_DEMANDS_ATTENTION
    set_state(states, kywc_view->closeable, CLOSEABLE);
    set_state(states, kywc_view->minimizable, MINIMIZABLE);
    set_state(states, kywc_view->maximizable, MAXIMIZABLE);
    set_state(states, kywc_view->fullscreenable, FULLSCREENABLE);
    set_state(states, kywc_view->skip_taskbar, SKIPTASKBAR);
    // ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_SHADEABLE
    // ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_SHADED
    set_state(states, kywc_view->movable, MOVABLE);
    set_state(states, kywc_view->resizable, RESIZABLE);
    // ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_VIRTUAL_DESKTOP_CHANGEABLE
    set_state(states, kywc_view->skip_switcher, SKIPSWITCHER);

    if (force || states != window->states) {
        window->states = states;
        if (resource) {
            org_kde_plasma_window_send_state_changed(resource, window->states);
        } else {
            struct wl_resource *resource;
            wl_resource_for_each(resource, &window->resources) {
                org_kde_plasma_window_send_state_changed(resource, window->states);
            }
        }
    }
}

#undef set_state

static void kde_plasma_window_add_resource(struct kde_plasma_window *window,
                                           struct wl_resource *management_resource, uint32_t id)
{
    struct wl_client *client = wl_resource_get_client(management_resource);
    uint32_t version = wl_resource_get_version(management_resource);
    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_plasma_window_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_list_insert(&window->resources, &resource->link);
    wl_resource_set_implementation(resource, &kde_plasma_window_impl, window,
                                   window_handle_resource_destroy);

    /* send states */
    kde_plasma_window_send_state(window, resource, true);

    struct kywc_view *kywc_view = window->kywc_view;
    struct view *view = view_from_kywc_view(kywc_view);
    if (kywc_view->title) {
        org_kde_plasma_window_send_title_changed(resource, kywc_view->title);
    }
    if (kywc_view->app_id) {
        org_kde_plasma_window_send_app_id_changed(resource, kywc_view->app_id);
    }
    if (!kywc_view->mapped) {
        org_kde_plasma_window_send_unmapped(resource);
    }
    if (view->pid) {
        org_kde_plasma_window_send_pid_changed(resource, view->pid);
    }

    org_kde_plasma_window_send_geometry(resource, kywc_view->geometry.x, kywc_view->geometry.y,
                                        kywc_view->geometry.width, kywc_view->geometry.height);

    // org_kde_plasma_window_send_parent_window
    // org_kde_plasma_window_send_virtual_desktop_changed
    // org_kde_plasma_window_send_virtual_desktop_entered
    // org_kde_plasma_window_send_virtual_desktop_left

    // org_kde_plasma_window_send_themed_icon_name_changed
    // org_kde_plasma_window_send_icon_changed
    // org_kde_plasma_window_send_application_menu
    // org_kde_plasma_window_send_activity_entered
    // org_kde_plasma_window_send_activity_left
    // org_kde_plasma_window_send_resource_name_changed

    org_kde_plasma_window_send_initial_state(resource);
}

/* deprecated */
static void handle_get_window(struct wl_client *client, struct wl_resource *management_resource,
                              uint32_t id, uint32_t internal_window_id)
{
    struct kde_plasma_window_management *management =
        wl_resource_get_user_data(management_resource);
    struct kde_plasma_window *window = kde_plasma_window_from_id(management, internal_window_id);
    if (!window) {
        return;
    }

    kde_plasma_window_add_resource(window, management_resource, id);
}

static void handle_get_window_by_uuid(struct wl_client *client,
                                      struct wl_resource *management_resource, uint32_t id,
                                      const char *internal_window_uuid)
{
    struct kde_plasma_window_management *management =
        wl_resource_get_user_data(management_resource);
    struct kde_plasma_window *window =
        kde_plasma_window_from_uuid(management, internal_window_uuid);
    if (!window) {
        return;
    }

    kde_plasma_window_add_resource(window, management_resource, id);
}

static void handle_show_desktop(struct wl_client *client, struct wl_resource *resource,
                                uint32_t state)
{
    view_manager_show_desktop(state == ORG_KDE_PLASMA_WINDOW_MANAGEMENT_SHOW_DESKTOP_ENABLED, true);
}

static const struct org_kde_plasma_window_management_interface kde_plasma_window_management_impl = {
    .show_desktop = handle_show_desktop,
    .get_window = handle_get_window,
    .get_window_by_uuid = handle_get_window_by_uuid,
};

static void management_handle_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(&resource->link);
}

static void kde_plasma_window_management_bind(struct wl_client *client, void *data,
                                              uint32_t version, uint32_t id)
{
    struct kde_plasma_window_management *management = data;

    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_plasma_window_management_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_list_insert(&management->resources, &resource->link);
    wl_resource_set_implementation(resource, &kde_plasma_window_management_impl, management,
                                   management_handle_resource_destroy);

    org_kde_plasma_window_management_send_show_desktop_changed(
        resource, view_manager_get_show_desktop()
                      ? ORG_KDE_PLASMA_WINDOW_MANAGEMENT_SHOW_DESKTOP_ENABLED
                      : ORG_KDE_PLASMA_WINDOW_MANAGEMENT_SHOW_DESKTOP_DISABLED);

    struct kde_plasma_window *window;
    wl_list_for_each(window, &management->windows, link) {
        if (!window->kywc_view->mapped) {
            continue;
        }
        // org_kde_plasma_window_management_send_window(resource, window->id);
        org_kde_plasma_window_management_send_window_with_uuid(resource, window->id, window->uuid);
    }

    // TODO: stacking_order_changed, stacking_order_uuid_changed
}

static void window_handle_view_map(struct wl_listener *listener, void *data)
{
    struct kde_plasma_window *window = wl_container_of(listener, window, view_map);
    struct kde_plasma_window_management *management = window->management;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &management->resources) {
        // org_kde_plasma_window_management_send_window(resource, window->id);
        org_kde_plasma_window_management_send_window_with_uuid(resource, window->id, window->uuid);
    }
}

static void window_handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct kde_plasma_window *window = wl_container_of(listener, window, view_destroy);

    wl_list_remove(&window->view_destroy.link);
    wl_list_remove(&window->view_map.link);
    wl_list_remove(&window->view_unmap.link);
    wl_list_remove(&window->view_title.link);
    wl_list_remove(&window->view_app_id.link);
    wl_list_remove(&window->view_activate.link);
    wl_list_remove(&window->view_minimize.link);
    wl_list_remove(&window->view_maximize.link);
    wl_list_remove(&window->view_fullscreen.link);
    wl_list_remove(&window->view_capabilities.link);
    wl_list_remove(&window->link);

    struct wl_resource *resource, *tmp;
    wl_resource_for_each_safe(resource, tmp, &window->resources) {
        window_handle_resource_destroy(resource);
    }

    free((void *)window->uuid);
    free(window);
}

static void handle_new_view(struct wl_listener *listener, void *data)
{
    struct kde_plasma_window_management *management =
        wl_container_of(listener, management, new_view);
    struct kywc_view *kywc_view = data;

    struct kde_plasma_window *window = calloc(1, sizeof(struct kde_plasma_window));
    if (!window) {
        return;
    }

    window->management = management;
    wl_list_init(&window->resources);
    wl_list_insert(&management->windows, &window->link);

    window->id = management->window_id_counter++;
    window->uuid = kywc_identifier_uuid_generate();

    window->kywc_view = kywc_view;
    window->view_map.notify = window_handle_view_map;
    wl_signal_add(&kywc_view->events.map, &window->view_map);
    window->view_unmap.notify = window_handle_view_unmap;
    wl_signal_add(&kywc_view->events.unmap, &window->view_unmap);
    window->view_destroy.notify = window_handle_view_destroy;
    wl_signal_add(&kywc_view->events.destroy, &window->view_destroy);
    window->view_title.notify = window_handle_view_title;
    wl_signal_add(&kywc_view->events.title, &window->view_title);
    window->view_app_id.notify = window_handle_view_app_id;
    wl_signal_add(&kywc_view->events.app_id, &window->view_app_id);
    window->view_activate.notify = window_handle_view_activate;
    wl_signal_add(&kywc_view->events.activate, &window->view_activate);
    window->view_minimize.notify = window_handle_view_minimize;
    wl_signal_add(&kywc_view->events.minimize, &window->view_minimize);
    window->view_maximize.notify = window_handle_view_maximize;
    wl_signal_add(&kywc_view->events.maximize, &window->view_maximize);
    window->view_fullscreen.notify = window_handle_view_fullscreen;
    wl_signal_add(&kywc_view->events.fullscreen, &window->view_fullscreen);
    window->view_capabilities.notify = window_handle_view_capabilities;
    wl_signal_add(&kywc_view->events.capabilities, &window->view_capabilities);
}

static void handle_shown_desktop(struct wl_listener *listener, void *data)
{
    struct kde_plasma_window_management *management =
        wl_container_of(listener, management, show_desktop);
    bool enabled = view_manager_get_show_desktop();

    struct wl_resource *resource;
    wl_resource_for_each(resource, &management->resources) {
        org_kde_plasma_window_management_send_show_desktop_changed(
            resource, enabled ? ORG_KDE_PLASMA_WINDOW_MANAGEMENT_SHOW_DESKTOP_ENABLED
                              : ORG_KDE_PLASMA_WINDOW_MANAGEMENT_SHOW_DESKTOP_DISABLED);
    }
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct kde_plasma_window_management *management =
        wl_container_of(listener, management, display_destroy);
    wl_list_remove(&management->display_destroy.link);
    wl_list_remove(&management->new_view.link);
    wl_list_remove(&management->show_desktop.link);
    wl_global_destroy(management->global);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct kde_plasma_window_management *management =
        wl_container_of(listener, management, server_destroy);
    wl_list_remove(&management->server_destroy.link);
    free(management);
}

bool kde_plasma_window_management_create(struct server *server)
{
    struct kde_plasma_window_management *management =
        calloc(1, sizeof(struct kde_plasma_window_management));
    if (!management) {
        return false;
    }

    management->global = wl_global_create(
        server->display, &org_kde_plasma_window_management_interface,
        PLASMA_WINDOW_MANAGEMENT_VERSION, management, kde_plasma_window_management_bind);
    if (!management->global) {
        kywc_log(KYWC_WARN, "kde plasma window management create failed");
        free(management);
        return false;
    }

    wl_list_init(&management->windows);
    wl_list_init(&management->resources);
    management->window_id_counter = 0;

    management->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &management->server_destroy);
    management->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &management->display_destroy);

    management->new_view.notify = handle_new_view;
    kywc_view_add_new_listener(&management->new_view);
    management->show_desktop.notify = handle_shown_desktop;
    kywc_view_add_show_desktop_listener(&management->show_desktop);

    return true;
}
