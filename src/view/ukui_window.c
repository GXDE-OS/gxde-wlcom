// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kywc/identifier.h>
#include <kywc/output.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>

#include "input/seat.h"
#include "theme.h"
#include "ukui-window-management-protocol.h"
#include "view/workspace.h"
#include "view_p.h"

#define UKUI_WINDOW_MANAGEMENT_VERSION 1

struct ukui_window_management {
    struct wl_global *global;
    struct wl_list resources;
    struct wl_list windows;

    struct ukui_window *highlight_window;

    struct wl_listener new_mapped_view;
    struct wl_listener show_desktop;
    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct ukui_window {
    struct wl_list resources;
    struct wl_list link;
    struct ukui_window_management *management;

    struct kywc_view *kywc_view;
    struct wl_listener view_unmap;
    struct wl_listener view_title;
    struct wl_listener view_app_id;
    struct wl_listener view_activate;
    struct wl_listener view_minimize;
    struct wl_listener view_maximize;
    struct wl_listener view_fullscreen;
    struct wl_listener view_capabilities;
    struct wl_listener workspace_enter;
    struct wl_listener workspace_leave;
    struct wl_listener view_position;
    struct wl_listener view_size;
    struct wl_listener panel_surface_destroy;

    /* The internal window id and uuid */
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
    STATE_FLAG_SHADEABLE,
    STATE_FLAG_SHADED,
    STATE_FLAG_MOVABLE,
    STATE_FLAG_RESIZABLE,
    STATE_FLAG_VIRTUAL_DESKTOP_CHANGEABLE,
    STATE_FLAG_ACCEPT_FOCUS,
    STATE_FLAG_SKIPTASKBAR,
    STATE_FLAG_SKIPSWITCHER,
    STATE_FLAG_MODALITY,
    STATE_FLAG_LAST,
};

static void ukui_window_set_state(struct ukui_window *window, enum state_flag flag, bool state)
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
    case STATE_FLAG_ACCEPT_FOCUS:
        window->kywc_view->focusable = state;
        break;
    case STATE_FLAG_SKIPTASKBAR:
        window->kywc_view->skip_taskbar = state;
        break;
    case STATE_FLAG_SKIPSWITCHER:
        window->kywc_view->skip_switcher = state;
        break;
    case STATE_FLAG_MODALITY:
        window->kywc_view->modal = state;
        break;
    case STATE_FLAG_LAST:
        break;
    }
}

static void ukui_window_send_state(struct ukui_window *window, struct wl_resource *resource,
                                   bool force);

static void handle_set_state(struct wl_client *client, struct wl_resource *resource, uint32_t flags,
                             uint32_t state)
{
    struct ukui_window *window = wl_resource_get_user_data(resource);
    if (!window) {
        return;
    }
    for (int i = 0; i < STATE_FLAG_LAST; i++) {
        if ((flags >> i) & 0x1) {
            ukui_window_set_state(window, i, (state >> i) & 0x1);
        }
    }
    ukui_window_send_state(window, NULL, false);
}

static void handle_set_startup_geometry(struct wl_client *client, struct wl_resource *resource,
                                        struct wl_resource *entry, uint32_t x, uint32_t y,
                                        uint32_t width, uint32_t height)
{
}

static void handle_panel_surface_destroy(struct wl_listener *listener, void *data)
{
    struct ukui_window *window = wl_container_of(listener, window, panel_surface_destroy);
    wl_list_remove(&window->panel_surface_destroy.link);
    wl_list_init(&window->panel_surface_destroy.link);
    struct view *view = view_from_kywc_view(window->kywc_view);
    view->minimized_geometry.panel_surface = NULL;
}

static void handle_set_minimized_geometry(struct wl_client *client, struct wl_resource *resource,
                                          struct wl_resource *panel, uint32_t x, uint32_t y,
                                          uint32_t width, uint32_t height)
{
    struct ukui_window *window = wl_resource_get_user_data(resource);
    struct wlr_surface *panel_surface = wlr_surface_from_resource(panel);
    if (!window || !panel_surface) {
        return;
    }

    struct view *view = view_from_kywc_view(window->kywc_view);
    view->minimized_geometry.panel_surface = panel_surface;
    view->minimized_geometry.x = x;
    view->minimized_geometry.y = y;
    view->minimized_geometry.width = width;
    view->minimized_geometry.height = height;

    wl_list_remove(&window->panel_surface_destroy.link);
    wl_signal_add(&panel_surface->events.destroy, &window->panel_surface_destroy);
}

static void handle_unset_minimized_geometry(struct wl_client *client, struct wl_resource *resource,
                                            struct wl_resource *panel)
{
    struct ukui_window *window = wl_resource_get_user_data(resource);
    struct wlr_surface *panel_surface = wlr_surface_from_resource(panel);
    if (!window || !panel_surface) {
        return;
    }

    struct view *view = view_from_kywc_view(window->kywc_view);
    if (view->minimized_geometry.panel_surface &&
        view->minimized_geometry.panel_surface == panel_surface) {
        wl_list_remove(&window->panel_surface_destroy.link);
        wl_list_init(&window->panel_surface_destroy.link);
        view->minimized_geometry.panel_surface = NULL;
    }
}

static void handle_close(struct wl_client *client, struct wl_resource *resource)
{
    struct ukui_window *window = wl_resource_get_user_data(resource);
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
    struct ukui_window *window = wl_resource_get_user_data(resource);
    if (!window) {
        return;
    }

    struct view *view = view_from_kywc_view(window->kywc_view);
    if (!view->impl->get_icon_buffer) {
        return;
    }

    float scale = view->output->state.scale;
    struct wlr_buffer *wlr_buffer = view->impl->get_icon_buffer(view, scale);
    if (!wlr_buffer) {
        return;
    }

    void *data;
    uint32_t format;
    size_t stride;
    if (wlr_buffer->impl->begin_data_ptr_access &&
        wlr_buffer->impl->begin_data_ptr_access(wlr_buffer, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &data,
                                                &format, &stride)) {
        int32_t size[2];
        size[0] = wlr_buffer->width;
        size[1] = wlr_buffer->height;
        write(fd, size, sizeof(size));
        write(fd, data, stride * wlr_buffer->height);
        close(fd);
        wlr_buffer->impl->end_data_ptr_access(wlr_buffer);
    }
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

static void ukui_window_highlight(struct ukui_window *ukui_window, bool enable)
{
    struct workspace *workspace = workspace_manager_get_current();
    struct view_proxy *view_proxy;
    wl_list_for_each_reverse(view_proxy, &workspace->view_proxies, workspace_link) {
        if (!view_proxy->view->base.mapped || view_proxy->view->base.minimized) {
            continue;
        }
        ky_scene_node_set_enabled(&view_proxy->view->tree->node, !enable);
    }

    if (!ukui_window) {
        return;
    }
    struct view *highlight_view = view_from_kywc_view(ukui_window->kywc_view);
    // Unset highlight window need to restore status when highlight window is minimized
    ky_scene_node_set_enabled(&highlight_view->tree->node,
                              enable ? enable : !highlight_view->base.minimized);
}

static void handle_request_highlight(struct wl_client *client, struct wl_resource *resource)
{
    struct ukui_window *window = wl_resource_get_user_data(resource);
    if (!window || window->management->highlight_window == window) {
        return;
    }

    if (window->management->highlight_window) {
        ukui_window_highlight(window->management->highlight_window, false);
    }
    ukui_window_highlight(window, true);
    window->management->highlight_window = window;
}

static void handle_request_unset_highlight(struct wl_client *client, struct wl_resource *resource)
{
    struct ukui_window *window = wl_resource_get_user_data(resource);
    if (!window || window->management->highlight_window != window) {
        return;
    }

    ukui_window_highlight(window, false);
    window->management->highlight_window = NULL;
}

static const struct ukui_window_interface ukui_window_impl = {
    .set_state = handle_set_state,
    .set_startup_geometry = handle_set_startup_geometry,
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
    .highlight = handle_request_highlight,
    .unset_highlight = handle_request_unset_highlight,
};

static struct ukui_window *ukui_window_from_uuid(struct ukui_window_management *management,
                                                 const char *uuid)
{
    struct ukui_window *window;
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
    wl_list_remove(wl_resource_get_link(resource));
}

static void window_handle_view_title(struct wl_listener *listener, void *data)
{
    struct ukui_window *window = wl_container_of(listener, window, view_title);
    if (!window->kywc_view->title) {
        return;
    }

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        ukui_window_send_title_changed(resource, window->kywc_view->title);
    }
}

static void window_handle_view_app_id(struct wl_listener *listener, void *data)
{
    struct ukui_window *window = wl_container_of(listener, window, view_app_id);
    if (!window->kywc_view->app_id) {
        return;
    }

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        ukui_window_send_app_id_changed(resource, window->kywc_view->app_id);
    }
}

#define set_state(states, prop, state)                                                             \
    if (prop) {                                                                                    \
        states |= UKUI_WINDOW_STATE_##state;                                                       \
    } else {                                                                                       \
        states &= ~UKUI_WINDOW_STATE_##state;                                                      \
    }

static void window_handle_view_activate(struct wl_listener *listener, void *data)
{
    struct ukui_window *window = wl_container_of(listener, window, view_activate);
    set_state(window->states, window->kywc_view->activated, ACTIVE);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        ukui_window_send_state_changed(resource, window->states);
    }
}

static void window_handle_view_minimize(struct wl_listener *listener, void *data)
{
    struct ukui_window *window = wl_container_of(listener, window, view_minimize);
    set_state(window->states, window->kywc_view->minimized, MINIMIZED);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        ukui_window_send_state_changed(resource, window->states);
    }
}

static void window_handle_view_maximize(struct wl_listener *listener, void *data)
{
    struct ukui_window *window = wl_container_of(listener, window, view_maximize);
    set_state(window->states, window->kywc_view->maximized, MAXIMIZED);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        ukui_window_send_state_changed(resource, window->states);
    }
}

static void window_handle_view_fullscreen(struct wl_listener *listener, void *data)
{
    struct ukui_window *window = wl_container_of(listener, window, view_fullscreen);
    set_state(window->states, window->kywc_view->fullscreen, FULLSCREEN);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        ukui_window_send_state_changed(resource, window->states);
    }
}

static void window_handle_view_capabilities(struct wl_listener *listener, void *data)
{
    struct ukui_window *window = wl_container_of(listener, window, view_capabilities);
    ukui_window_send_state(window, NULL, false);
}
static void window_handle_view_position(struct wl_listener *listener, void *data)
{
    struct ukui_window *window = wl_container_of(listener, window, view_position);
    struct kywc_view *view = window->kywc_view;

    int32_t x = view->geometry.x - view->margin.off_x;
    int32_t y = view->geometry.y - view->margin.off_y;
    uint32_t width = view->geometry.width + view->margin.off_width;
    uint32_t height = view->geometry.height + view->margin.off_height;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        ukui_window_send_geometry(resource, x, y, width, height);
    }
}

static void window_handle_view_size(struct wl_listener *listener, void *data)
{
    struct ukui_window *window = wl_container_of(listener, window, view_size);
    struct kywc_view *view = window->kywc_view;

    int32_t x = view->geometry.x - view->margin.off_x;
    int32_t y = view->geometry.y - view->margin.off_y;
    uint32_t width = view->geometry.width + view->margin.off_width;
    uint32_t height = view->geometry.height + view->margin.off_height;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        ukui_window_send_geometry(resource, x, y, width, height);
    }
}

static void ukui_window_send_state(struct ukui_window *window, struct wl_resource *resource,
                                   bool force)
{
    struct kywc_view *kywc_view = window->kywc_view;
    uint32_t states = window->states;

    set_state(states, kywc_view->activated, ACTIVE);
    set_state(states, kywc_view->minimized, MINIMIZED);
    set_state(states, kywc_view->maximized, MAXIMIZED);
    set_state(states, kywc_view->fullscreen, FULLSCREEN);
    set_state(states, kywc_view->kept_above, KEEP_ABOVE);
    set_state(states, kywc_view->kept_below, KEEP_BELOW);
    // ukui_window_MANAGEMENT_STATE_ON_ALL_DESKTOPS
    // ukui_window_MANAGEMENT_STATE_DEMANDS_ATTENTION
    set_state(states, kywc_view->closeable, CLOSEABLE);
    set_state(states, kywc_view->minimizable, MINIMIZABLE);
    set_state(states, kywc_view->maximizable, MAXIMIZABLE);
    set_state(states, kywc_view->fullscreenable, FULLSCREENABLE);
    set_state(states, kywc_view->skip_taskbar, SKIPTASKBAR);
    // ukui_window_MANAGEMENT_STATE_SHADEABLE
    // ukui_window_MANAGEMENT_STATE_SHADED
    set_state(states, kywc_view->movable, MOVABLE);
    set_state(states, kywc_view->resizable, RESIZABLE);
    // ukui_window_MANAGEMENT_STATE_VIRTUAL_DESKTOP_CHANGEABLE
    set_state(states, kywc_view->skip_switcher, SKIPSWITCHER);
    set_state(states, kywc_view->focusable, ACCEPT_FOCUS);
    set_state(states, kywc_view->modal, MODALITY);

    if (force || states != window->states) {
        window->states = states;
        if (resource) {
            ukui_window_send_state_changed(resource, window->states);
        } else {
            struct wl_resource *resource;
            wl_resource_for_each(resource, &window->resources) {
                ukui_window_send_state_changed(resource, window->states);
            }
        }
    }
}

#undef set_state

static void ukui_window_add_resource(struct ukui_window *window,
                                     struct wl_resource *management_resource, uint32_t id)
{
    struct wl_client *client = wl_resource_get_client(management_resource);
    uint32_t version = wl_resource_get_version(management_resource);
    struct wl_resource *resource = wl_resource_create(client, &ukui_window_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &ukui_window_impl, window,
                                   window_handle_resource_destroy);

    if (!window) {
        wl_list_init(wl_resource_get_link(resource));
        ukui_window_send_unmapped(resource);
        return;
    }

    wl_list_insert(&window->resources, wl_resource_get_link(resource));

    /* send states */
    ukui_window_send_state(window, resource, true);

    struct kywc_view *kywc_view = window->kywc_view;
    struct view *view = view_from_kywc_view(kywc_view);
    if (kywc_view->title) {
        ukui_window_send_title_changed(resource, kywc_view->title);
    }
    if (kywc_view->app_id) {
        ukui_window_send_app_id_changed(resource, kywc_view->app_id);
    }
    if (view->pid) {
        ukui_window_send_pid_changed(resource, view->pid);
    }

    int32_t x = kywc_view->geometry.x - kywc_view->margin.off_x;
    int32_t y = kywc_view->geometry.y - kywc_view->margin.off_y;
    uint32_t width = kywc_view->geometry.width + kywc_view->margin.off_width;
    uint32_t height = kywc_view->geometry.height + kywc_view->margin.off_height;
    ukui_window_send_geometry(resource, x, y, width, height);

    // ukui_window_send_parent_window
    // ukui_window_send_virtual_desktop_changed
    // ukui_window_send_virtual_desktop_entered
    // ukui_window_send_virtual_desktop_left
    const char *icon_name = theme_icon_name(kywc_view->app_id);
    if (strcmp(icon_name, "fallback")) {
        ukui_window_send_themed_icon_name_changed(resource, icon_name);
    } else {
        ukui_window_send_icon_changed(resource);
    }
    // ukui_window_send_application_menu
    // ukui_window_send_activity_entered
    // ukui_window_send_activity_left
    // ukui_window_send_resource_name_changed

    ukui_window_send_initial_state(resource);
    struct view_proxy *proxy;
    wl_list_for_each(proxy, &view->view_proxies, view_link) {
        ukui_window_send_virtual_desktop_entered(resource, proxy->workspace->uuid);
    }
}

static void handle_create_window(struct wl_client *client, struct wl_resource *management_resource,
                                 uint32_t id, const char *internal_window_uuid)
{
    struct ukui_window_management *management = wl_resource_get_user_data(management_resource);
    struct ukui_window *window = ukui_window_from_uuid(management, internal_window_uuid);

    ukui_window_add_resource(window, management_resource, id);
}

static void handle_show_desktop(struct wl_client *client, struct wl_resource *resource,
                                uint32_t state)
{
    view_manager_show_desktop(state == UKUI_WINDOW_MANAGEMENT_SHOW_DESKTOP_ENABLED, true);
}

static const struct ukui_window_management_interface ukui_window_management_impl = {
    .show_desktop = handle_show_desktop,
    .create_window = handle_create_window,
};

static void management_handle_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static void ukui_window_management_bind(struct wl_client *client, void *data, uint32_t version,
                                        uint32_t id)
{
    struct ukui_window_management *management = data;

    struct wl_resource *resource =
        wl_resource_create(client, &ukui_window_management_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_list_insert(&management->resources, wl_resource_get_link(resource));
    wl_resource_set_implementation(resource, &ukui_window_management_impl, management,
                                   management_handle_resource_destroy);

    ukui_window_management_send_show_desktop_changed(
        resource, view_manager_get_show_desktop() ? UKUI_WINDOW_MANAGEMENT_SHOW_DESKTOP_ENABLED
                                                  : UKUI_WINDOW_MANAGEMENT_SHOW_DESKTOP_DISABLED);

    struct ukui_window *window;
    wl_list_for_each(window, &management->windows, link) {
        ukui_window_management_send_window_created(resource, window->uuid);
    }

    // TODO: stacking_order_changed
}

static void handle_window_workspace_enter(struct wl_listener *listener, void *data)
{
    struct ukui_window *window = wl_container_of(listener, window, workspace_enter);
    struct workspace *workspace = data;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        ukui_window_send_virtual_desktop_entered(resource, workspace->uuid);
    }
}

static void handle_window_workspace_leave(struct wl_listener *listener, void *data)
{
    struct ukui_window *window = wl_container_of(listener, window, workspace_leave);
    struct workspace *workspace = data;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        ukui_window_send_virtual_desktop_left(resource, workspace->uuid);
    }
}

static void window_handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct ukui_window *window = wl_container_of(listener, window, view_unmap);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &window->resources) {
        ukui_window_send_unmapped(resource);
    }

    wl_list_remove(&window->view_unmap.link);
    wl_list_remove(&window->view_title.link);
    wl_list_remove(&window->view_app_id.link);
    wl_list_remove(&window->view_activate.link);
    wl_list_remove(&window->view_minimize.link);
    wl_list_remove(&window->view_maximize.link);
    wl_list_remove(&window->view_fullscreen.link);
    wl_list_remove(&window->view_capabilities.link);
    wl_list_remove(&window->workspace_enter.link);
    wl_list_remove(&window->workspace_leave.link);
    wl_list_remove(&window->view_position.link);
    wl_list_remove(&window->view_size.link);
    wl_list_remove(&window->panel_surface_destroy.link);
    wl_list_remove(&window->link);

    struct wl_resource *tmp;
    wl_resource_for_each_safe(resource, tmp, &window->resources) {
        window_handle_resource_destroy(resource);
    }

    if (window->management->highlight_window == window) {
        window->management->highlight_window = NULL;
        ukui_window_highlight(window->management->highlight_window, false);
    }

    free(window);
}

static void handle_new_mapped_view(struct wl_listener *listener, void *data)
{
    struct ukui_window_management *management =
        wl_container_of(listener, management, new_mapped_view);
    struct kywc_view *kywc_view = data;
    struct view *view = view_from_kywc_view(kywc_view);

    struct ukui_window *window = calloc(1, sizeof(struct ukui_window));
    if (!window) {
        return;
    }

    window->management = management;
    wl_list_init(&window->resources);
    wl_list_insert(&management->windows, &window->link);

    window->uuid = kywc_view->uuid;

    window->kywc_view = kywc_view;
    window->view_unmap.notify = window_handle_view_unmap;
    wl_signal_add(&kywc_view->events.unmap, &window->view_unmap);
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
    window->workspace_enter.notify = handle_window_workspace_enter;
    wl_signal_add(&view->events.workspace_enter, &window->workspace_enter);
    window->workspace_leave.notify = handle_window_workspace_leave;
    wl_signal_add(&view->events.workspace_leave, &window->workspace_leave);
    window->view_position.notify = window_handle_view_position;
    wl_signal_add(&kywc_view->events.position, &window->view_position);
    window->view_size.notify = window_handle_view_size;
    wl_signal_add(&kywc_view->events.size, &window->view_size);
    window->panel_surface_destroy.notify = handle_panel_surface_destroy;
    wl_list_init(&window->panel_surface_destroy.link);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &management->resources) {
        ukui_window_management_send_window_created(resource, window->uuid);
    }

    if (management->highlight_window) {
        ukui_window_highlight(management->highlight_window, false);
        management->highlight_window = NULL;
    }
}

static void handle_shown_desktop(struct wl_listener *listener, void *data)
{
    struct ukui_window_management *management = wl_container_of(listener, management, show_desktop);
    bool enabled = view_manager_get_show_desktop();

    struct wl_resource *resource;
    wl_resource_for_each(resource, &management->resources) {
        ukui_window_management_send_show_desktop_changed(
            resource, enabled ? UKUI_WINDOW_MANAGEMENT_SHOW_DESKTOP_ENABLED
                              : UKUI_WINDOW_MANAGEMENT_SHOW_DESKTOP_DISABLED);
    }
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct ukui_window_management *management =
        wl_container_of(listener, management, display_destroy);
    wl_list_remove(&management->display_destroy.link);
    wl_list_remove(&management->new_mapped_view.link);
    wl_list_remove(&management->show_desktop.link);
    wl_global_destroy(management->global);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct ukui_window_management *management =
        wl_container_of(listener, management, server_destroy);
    wl_list_remove(&management->server_destroy.link);
    free(management);
}

bool ukui_window_management_create(struct server *server)
{
    struct ukui_window_management *management = calloc(1, sizeof(struct ukui_window_management));
    if (!management) {
        return false;
    }

    management->global =
        wl_global_create(server->display, &ukui_window_management_interface,
                         UKUI_WINDOW_MANAGEMENT_VERSION, management, ukui_window_management_bind);
    if (!management->global) {
        kywc_log(KYWC_WARN, "ukui window management create failed");
        free(management);
        return false;
    }

    wl_list_init(&management->windows);
    wl_list_init(&management->resources);

    management->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &management->server_destroy);
    management->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &management->display_destroy);

    management->new_mapped_view.notify = handle_new_mapped_view;
    kywc_view_add_new_mapped_listener(&management->new_mapped_view);
    management->show_desktop.notify = handle_shown_desktop;
    view_add_show_desktop_listener(&management->show_desktop);

    return true;
}
