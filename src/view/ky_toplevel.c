// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include <kywc/output.h>

#include "kywc-toplevel-v1-protocol.h"

#include "theme.h"
#include "view/workspace.h"
#include "view_p.h"

struct ky_toplevel_manager {
    struct wl_event_loop *event_loop;
    struct wl_global *global;
    struct wl_list resources;
    struct wl_list toplevels;

    struct wl_listener new_mapped_toplevel;
    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct ky_toplevel {
    struct ky_toplevel_manager *manager;
    struct wl_list link;

    struct wl_event_source *idle_source;
    struct wl_list resources;

    const char *icon_name;

    struct kywc_view *view;
    struct wl_listener unmap;

    struct wl_listener title;
    struct wl_listener app_id;
    struct wl_listener maximize;
    struct wl_listener minimize;
    struct wl_listener activate;
    struct wl_listener fullscreen;

    struct wl_listener position;
    struct wl_listener size;

    struct wl_listener icon_update;
    struct wl_listener output;
    struct wl_listener parent;
    struct wl_listener workspace_enter;
    struct wl_listener workspace_leave;
};

static struct ky_toplevel *toplevel_for_view(struct ky_toplevel_manager *manager,
                                             struct kywc_view *view)
{
    struct ky_toplevel *toplevel;
    wl_list_for_each(toplevel, &manager->toplevels, link) {
        if (toplevel->view == view) {
            return toplevel;
        }
    }
    return NULL;
}

static void toplevel_update_icon_name(struct ky_toplevel *toplevel)
{
    struct view *view = view_from_kywc_view(toplevel->view);
    const char *icon_name = theme_icon_get_name(view->icon);
    if (toplevel->icon_name && strcmp(toplevel->icon_name, icon_name) == 0) {
        return;
    }

    free((void *)toplevel->icon_name);
    toplevel->icon_name = strdup(icon_name);
}

static void toplevel_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void toplevel_handle_set_maximized(struct wl_client *client, struct wl_resource *resource,
                                          const char *output)
{
    struct ky_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (!toplevel) {
        return;
    }

    struct kywc_output *kywc_output = NULL;
    if (output) {
        kywc_output = kywc_output_by_uuid(output);
    }
    kywc_view_set_maximized(toplevel->view, true, kywc_output);
}

static void toplevel_handle_unset_maximized(struct wl_client *client, struct wl_resource *resource)
{
    struct ky_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (!toplevel) {
        return;
    }

    kywc_view_set_maximized(toplevel->view, false, NULL);
}

static void toplevel_handle_set_minimized(struct wl_client *client, struct wl_resource *resource)
{
    struct ky_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (!toplevel) {
        return;
    }

    kywc_view_set_minimized(toplevel->view, true);
}

static void toplevel_handle_unset_minimized(struct wl_client *client, struct wl_resource *resource)
{
    struct ky_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (!toplevel) {
        return;
    }

    kywc_view_set_minimized(toplevel->view, false);
}

static void toplevel_handle_set_fullscreen(struct wl_client *client, struct wl_resource *resource,
                                           const char *output)
{
    struct ky_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (!toplevel) {
        return;
    }

    struct kywc_output *kywc_output = NULL;
    if (output) {
        kywc_output = kywc_output_by_uuid(output);
    }
    kywc_view_set_fullscreen(toplevel->view, true, kywc_output);
}

static void toplevel_handle_unset_fullscreen(struct wl_client *client, struct wl_resource *resource)
{
    struct ky_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (!toplevel) {
        return;
    }

    kywc_view_set_fullscreen(toplevel->view, false, NULL);
}

static void toplevel_handle_activate(struct wl_client *client, struct wl_resource *resource)
{
    struct ky_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (!toplevel) {
        return;
    }

    kywc_view_activate(toplevel->view);
    view_set_focus(view_from_kywc_view(toplevel->view), input_manager_get_default_seat());
}

static void toplevel_handle_close(struct wl_client *client, struct wl_resource *resource)
{
    struct ky_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (!toplevel) {
        return;
    }

    kywc_view_close(toplevel->view);
}

static void toplevel_handle_enter_workspace(struct wl_client *client, struct wl_resource *resource,
                                            const char *id)
{
    struct ky_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (!toplevel) {
        return;
    }

    struct workspace *workspace = workspace_by_uuid(id);
    if (workspace) {
        view_add_workspace(view_from_kywc_view(toplevel->view), workspace);
    }
}

static void toplevel_handle_leave_workspace(struct wl_client *client, struct wl_resource *resource,
                                            const char *id)
{
    struct ky_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (!toplevel) {
        return;
    }

    struct workspace *workspace = workspace_by_uuid(id);
    if (workspace) {
        view_remove_workspace(view_from_kywc_view(toplevel->view), workspace);
    }
}

static void toplevel_handle_move_to_workspace(struct wl_client *client,
                                              struct wl_resource *resource, const char *id)
{
    struct ky_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (!toplevel) {
        return;
    }

    struct workspace *workspace = workspace_by_uuid(id);
    if (workspace) {
        view_set_workspace(view_from_kywc_view(toplevel->view), workspace);
    }
}

static void toplevel_handle_move_to_output(struct wl_client *client, struct wl_resource *resource,
                                           const char *id)
{
    struct ky_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (!toplevel) {
        return;
    }

    struct kywc_output *kywc_output = kywc_output_by_uuid(id);
    if (kywc_output) {
        view_move_to_output(view_from_kywc_view(toplevel->view), NULL, kywc_output);
    }
}

static void toplevel_handle_set_position(struct wl_client *client, struct wl_resource *resource,
                                         int32_t x, int32_t y)
{
    struct ky_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (!toplevel) {
        return;
    }

    kywc_view_move(toplevel->view, x, y);
}

static void toplevel_handle_set_size(struct wl_client *client, struct wl_resource *resource,
                                     uint32_t width, uint32_t height)
{
    struct ky_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (!toplevel) {
        return;
    }

    struct kywc_box geo = { toplevel->view->geometry.x, toplevel->view->geometry.y, width, height };
    kywc_view_resize(toplevel->view, &geo);
}

static const struct kywc_toplevel_v1_interface ky_toplevel_impl = {
    .destroy = toplevel_handle_destroy,
    .set_maximized = toplevel_handle_set_maximized,
    .unset_maximized = toplevel_handle_unset_maximized,
    .set_minimized = toplevel_handle_set_minimized,
    .unset_minimized = toplevel_handle_unset_minimized,
    .set_fullscreen = toplevel_handle_set_fullscreen,
    .unset_fullscreen = toplevel_handle_unset_fullscreen,
    .activate = toplevel_handle_activate,
    .close = toplevel_handle_close,
    .enter_workspace = toplevel_handle_enter_workspace,
    .leave_workspace = toplevel_handle_leave_workspace,
    .move_to_workspace = toplevel_handle_move_to_workspace,
    .move_to_output = toplevel_handle_move_to_output,
    .set_position = toplevel_handle_set_position,
    .set_size = toplevel_handle_set_size,
};

static void ky_toplevel_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static struct wl_resource *
create_toplevel_resource_for_resource(struct ky_toplevel *ky_toplevel,
                                      struct wl_resource *manager_resource)
{
    struct wl_client *client = wl_resource_get_client(manager_resource);
    struct wl_resource *resource = wl_resource_create(client, &kywc_toplevel_v1_interface,
                                                      wl_resource_get_version(manager_resource), 0);
    if (!resource) {
        wl_client_post_no_memory(client);
        return NULL;
    }

    wl_resource_set_implementation(resource, &ky_toplevel_impl, ky_toplevel,
                                   ky_toplevel_resource_destroy);

    wl_list_insert(&ky_toplevel->resources, wl_resource_get_link(resource));
    kywc_toplevel_manager_v1_send_toplevel(manager_resource, resource, ky_toplevel->view->uuid);
    return resource;
}

static uint32_t toplevel_state(struct kywc_view *kywc_view)
{
    uint32_t state = 0;
    if (kywc_view->maximized) {
        state |= KYWC_TOPLEVEL_V1_STATE_MAXIMIZED;
    }
    if (kywc_view->minimized) {
        state |= KYWC_TOPLEVEL_V1_STATE_MINIMIZED;
    }
    if (kywc_view->activated) {
        state |= KYWC_TOPLEVEL_V1_STATE_ACTIVATED;
    }
    if (kywc_view->fullscreen) {
        state |= KYWC_TOPLEVEL_V1_STATE_FULLSCREEN;
    }
    return state;
}

static uint32_t toplevel_caps(struct kywc_view *kywc_view)
{
    uint32_t caps = 0;
    if (kywc_view->skip_taskbar) {
        caps |= KYWC_TOPLEVEL_V1_CAPABILITY_SKIP_TASKBAR;
    }
    if (kywc_view->skip_switcher) {
        caps |= KYWC_TOPLEVEL_V1_CAPABILITY_SKIP_SWITCHER;
    }
    return caps;
}

static void toplevel_send_details_to_toplevel_resource(struct ky_toplevel *toplevel,
                                                       struct wl_resource *resource)
{
    struct kywc_view *kywc_view = toplevel->view;
    struct view *view = view_from_kywc_view(kywc_view);

    if (kywc_view->app_id) {
        kywc_toplevel_v1_send_app_id(resource, kywc_view->app_id);
    }

    if (kywc_view->title) {
        kywc_toplevel_v1_send_title(resource, kywc_view->title);
    }

    if (view->output) {
        kywc_toplevel_v1_send_primary_output(resource, view->output->uuid);
    }

    kywc_toplevel_v1_send_pid(resource, view->pid);

    /* all workspaces the toplevel in */
    struct view_proxy *proxy;
    wl_list_for_each(proxy, &view->view_proxies, view_link) {
        kywc_toplevel_v1_send_workspace_enter(resource, proxy->workspace->uuid);
    }

    kywc_toplevel_v1_send_capabilities(resource, toplevel_caps(toplevel->view));

    kywc_toplevel_v1_send_state(resource, toplevel_state(toplevel->view));

    if (view->parent) {
        struct ky_toplevel *parent_toplevel =
            toplevel_for_view(toplevel->manager, &view->parent->base);
        if (parent_toplevel) {
            struct wl_resource *parent_resource = wl_resource_find_for_client(
                &parent_toplevel->resources, wl_resource_get_client(resource));
            if (parent_resource) {
                kywc_toplevel_v1_send_parent(resource, parent_resource);
            }
        }
    } else {
        kywc_toplevel_v1_send_parent(resource, NULL);
    }

    kywc_toplevel_v1_send_icon(resource, toplevel->icon_name);

    int32_t x = kywc_view->geometry.x - kywc_view->margin.off_x;
    int32_t y = kywc_view->geometry.y - kywc_view->margin.off_y;
    uint32_t width = kywc_view->geometry.width + kywc_view->margin.off_width;
    uint32_t height = kywc_view->geometry.height + kywc_view->margin.off_height;
    kywc_toplevel_v1_send_geometry(resource, x, y, width, height);

    kywc_toplevel_v1_send_done(resource);
}

static void manager_handle_stop(struct wl_client *client, struct wl_resource *resource)
{
    kywc_toplevel_manager_v1_send_finished(resource);
    wl_resource_destroy(resource);
}

static const struct kywc_toplevel_manager_v1_interface ky_toplevel_manager_impl = {
    .stop = manager_handle_stop,
};

static void ky_toplevel_manager_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static void ky_toplevel_manager_bind(struct wl_client *client, void *data, uint32_t version,
                                     uint32_t id)
{
    struct ky_toplevel_manager *manager = data;
    struct wl_resource *resource =
        wl_resource_create(client, &kywc_toplevel_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &ky_toplevel_manager_impl, manager,
                                   ky_toplevel_manager_resource_destroy);
    wl_list_insert(&manager->resources, wl_resource_get_link(resource));

    /* send all toplevels and details */
    struct ky_toplevel *toplevel, *tmp;
    wl_list_for_each_reverse_safe(toplevel, tmp, &manager->toplevels, link) {
        create_toplevel_resource_for_resource(toplevel, resource);
    }
    wl_list_for_each_reverse_safe(toplevel, tmp, &manager->toplevels, link) {
        struct wl_resource *toplevel_resource =
            wl_resource_find_for_client(&toplevel->resources, client);
        toplevel_send_details_to_toplevel_resource(toplevel, toplevel_resource);
    }
}

static void toplevel_idle_send_done(void *data)
{
    struct ky_toplevel *toplevel = data;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        kywc_toplevel_v1_send_done(resource);
    }

    toplevel->idle_source = NULL;
}

static void toplevel_update_idle_source(struct ky_toplevel *toplevel)
{
    if (toplevel->idle_source || wl_list_empty(&toplevel->resources)) {
        return;
    }

    toplevel->idle_source =
        wl_event_loop_add_idle(toplevel->manager->event_loop, toplevel_idle_send_done, toplevel);
}

static void handle_toplevel_title(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = wl_container_of(listener, toplevel, title);
    if (!toplevel->view->title) {
        return;
    }

    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        kywc_toplevel_v1_send_title(resource, toplevel->view->title);
    }

    toplevel_update_idle_source(toplevel);
}

static void handle_toplevel_app_id(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = wl_container_of(listener, toplevel, app_id);
    if (!toplevel->view->app_id) {
        return;
    }

    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        kywc_toplevel_v1_send_app_id(resource, toplevel->view->app_id);
    }

    toplevel_update_idle_source(toplevel);
}

static void handle_toplevel_icon_update(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = wl_container_of(listener, toplevel, icon_update);

    toplevel_update_icon_name(toplevel);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        kywc_toplevel_v1_send_icon(resource, toplevel->icon_name);
    }

    toplevel_update_idle_source(toplevel);
}

static void handle_toplevel_maximize(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = wl_container_of(listener, toplevel, maximize);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        kywc_toplevel_v1_send_state(resource, toplevel_state(toplevel->view));
    }

    toplevel_update_idle_source(toplevel);
}

static void handle_toplevel_minimize(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = wl_container_of(listener, toplevel, minimize);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        kywc_toplevel_v1_send_state(resource, toplevel_state(toplevel->view));
    }

    toplevel_update_idle_source(toplevel);
}

static void handle_toplevel_activate(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = wl_container_of(listener, toplevel, activate);

    wl_list_remove(&toplevel->link);
    wl_list_insert(&toplevel->manager->toplevels, &toplevel->link);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        kywc_toplevel_v1_send_state(resource, toplevel_state(toplevel->view));
    }

    toplevel_update_idle_source(toplevel);
}

static void handle_toplevel_fullscreen(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = wl_container_of(listener, toplevel, fullscreen);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        kywc_toplevel_v1_send_state(resource, toplevel_state(toplevel->view));
    }

    toplevel_update_idle_source(toplevel);
}

static void handle_toplevel_position(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = wl_container_of(listener, toplevel, position);
    struct kywc_view *view = toplevel->view;

    int32_t x = view->geometry.x - view->margin.off_x;
    int32_t y = view->geometry.y - view->margin.off_y;
    uint32_t width = view->geometry.width + view->margin.off_width;
    uint32_t height = view->geometry.height + view->margin.off_height;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        kywc_toplevel_v1_send_geometry(resource, x, y, width, height);
    }

    toplevel_update_idle_source(toplevel);
}

static void handle_toplevel_size(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = wl_container_of(listener, toplevel, size);
    struct kywc_view *view = toplevel->view;

    int32_t x = view->geometry.x - view->margin.off_x;
    int32_t y = view->geometry.y - view->margin.off_y;
    uint32_t width = view->geometry.width + view->margin.off_width;
    uint32_t height = view->geometry.height + view->margin.off_height;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        kywc_toplevel_v1_send_geometry(resource, x, y, width, height);
    }

    toplevel_update_idle_source(toplevel);
}

static void handle_toplevel_output(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = wl_container_of(listener, toplevel, output);
    struct view *view = view_from_kywc_view(toplevel->view);
    if (!view->output) {
        return;
    }

    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        kywc_toplevel_v1_send_primary_output(resource, view->output->uuid);
    }

    toplevel_update_idle_source(toplevel);
}

static void handle_toplevel_parent(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = wl_container_of(listener, toplevel, parent);
    struct view *view = view_from_kywc_view(toplevel->view);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        if (view->parent) {
            struct ky_toplevel *parent_toplevel =
                toplevel_for_view(toplevel->manager, &view->parent->base);
            if (parent_toplevel) {
                struct wl_resource *parent_resource = wl_resource_find_for_client(
                    &parent_toplevel->resources, wl_resource_get_client(resource));
                if (parent_resource) {
                    kywc_toplevel_v1_send_parent(resource, parent_resource);
                }
            }
        } else {
            kywc_toplevel_v1_send_parent(resource, NULL);
        }
    }

    toplevel_update_idle_source(toplevel);
}

static void handle_toplevel_workspace_enter(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = wl_container_of(listener, toplevel, workspace_enter);
    struct workspace *workspace = data;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        kywc_toplevel_v1_send_workspace_enter(resource, workspace->uuid);
    }

    toplevel_update_idle_source(toplevel);
}

static void handle_toplevel_workspace_leave(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = wl_container_of(listener, toplevel, workspace_leave);
    struct workspace *workspace = data;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        kywc_toplevel_v1_send_workspace_leave(resource, workspace->uuid);
    }

    toplevel_update_idle_source(toplevel);
}

static void handle_toplevel_unmap(struct wl_listener *listener, void *data);

static void handle_new_mapped_toplevel(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    if (!toplevel) {
        return;
    }

    struct ky_toplevel_manager *manager = wl_container_of(listener, manager, new_mapped_toplevel);
    toplevel->manager = manager;
    wl_list_init(&toplevel->resources);
    wl_list_insert(&manager->toplevels, &toplevel->link);

    struct kywc_view *kywc_view = data;
    toplevel->view = kywc_view;

    toplevel->unmap.notify = handle_toplevel_unmap;
    wl_signal_add(&kywc_view->events.unmap, &toplevel->unmap);

    toplevel->title.notify = handle_toplevel_title;
    wl_signal_add(&toplevel->view->events.title, &toplevel->title);
    toplevel->app_id.notify = handle_toplevel_app_id;
    wl_signal_add(&toplevel->view->events.app_id, &toplevel->app_id);
    toplevel->maximize.notify = handle_toplevel_maximize;
    wl_signal_add(&toplevel->view->events.maximize, &toplevel->maximize);
    toplevel->minimize.notify = handle_toplevel_minimize;
    wl_signal_add(&toplevel->view->events.minimize, &toplevel->minimize);
    toplevel->activate.notify = handle_toplevel_activate;
    wl_signal_add(&toplevel->view->events.activate, &toplevel->activate);
    toplevel->fullscreen.notify = handle_toplevel_fullscreen;
    wl_signal_add(&toplevel->view->events.fullscreen, &toplevel->fullscreen);
    toplevel->position.notify = handle_toplevel_position;
    wl_signal_add(&toplevel->view->events.position, &toplevel->position);
    toplevel->size.notify = handle_toplevel_size;
    wl_signal_add(&toplevel->view->events.size, &toplevel->size);

    struct view *view = view_from_kywc_view(toplevel->view);
    toplevel->icon_update.notify = handle_toplevel_icon_update;
    wl_signal_add(&view->events.icon_update, &toplevel->icon_update);
    toplevel->output.notify = handle_toplevel_output;
    wl_signal_add(&view->events.output, &toplevel->output);
    toplevel->parent.notify = handle_toplevel_parent;
    wl_signal_add(&view->events.parent, &toplevel->parent);
    toplevel->workspace_enter.notify = handle_toplevel_workspace_enter;
    wl_signal_add(&view->events.workspace_enter, &toplevel->workspace_enter);
    toplevel->workspace_leave.notify = handle_toplevel_workspace_leave;
    wl_signal_add(&view->events.workspace_leave, &toplevel->workspace_leave);

    toplevel_update_icon_name(toplevel);

    /* send toplevel and detail */
    struct wl_resource *resource, *toplevel_resource;
    wl_resource_for_each(resource, &toplevel->manager->resources) {
        toplevel_resource = create_toplevel_resource_for_resource(toplevel, resource);
        toplevel_send_details_to_toplevel_resource(toplevel, toplevel_resource);
    }
}

static void handle_toplevel_unmap(struct wl_listener *listener, void *data)
{
    struct ky_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

    struct wl_resource *resource, *tmp;
    wl_resource_for_each_safe(resource, tmp, &toplevel->resources) {
        kywc_toplevel_v1_send_closed(resource);
        // make the resource inert
        wl_list_remove(wl_resource_get_link(resource));
        wl_list_init(wl_resource_get_link(resource));
        wl_resource_set_user_data(resource, NULL);
    }

    if (toplevel->idle_source) {
        wl_event_source_remove(toplevel->idle_source);
    }

    wl_list_remove(&toplevel->title.link);
    wl_list_remove(&toplevel->app_id.link);
    wl_list_remove(&toplevel->icon_update.link);
    wl_list_remove(&toplevel->maximize.link);
    wl_list_remove(&toplevel->minimize.link);
    wl_list_remove(&toplevel->activate.link);
    wl_list_remove(&toplevel->fullscreen.link);
    wl_list_remove(&toplevel->position.link);
    wl_list_remove(&toplevel->size.link);
    wl_list_remove(&toplevel->output.link);
    wl_list_remove(&toplevel->parent.link);
    wl_list_remove(&toplevel->workspace_enter.link);
    wl_list_remove(&toplevel->workspace_leave.link);

    free((void *)toplevel->icon_name);
    toplevel->icon_name = NULL;

    wl_list_remove(&toplevel->unmap.link);
    wl_list_remove(&toplevel->link);
    free(toplevel);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct ky_toplevel_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct ky_toplevel_manager *manager = wl_container_of(listener, manager, display_destroy);
    wl_list_remove(&manager->new_mapped_toplevel.link);
    wl_list_remove(&manager->display_destroy.link);
    wl_global_destroy(manager->global);
}

bool ky_toplevel_manager_create(struct server *server)
{
    struct ky_toplevel_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    manager->global = wl_global_create(server->display, &kywc_toplevel_manager_v1_interface, 1,
                                       manager, ky_toplevel_manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_WARN, "kywc toplevel manager create failed");
        free(manager);
        return false;
    }

    wl_list_init(&manager->resources);
    wl_list_init(&manager->toplevels);

    manager->event_loop = wl_display_get_event_loop(server->display);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    manager->new_mapped_toplevel.notify = handle_new_mapped_toplevel;
    kywc_view_add_new_mapped_listener(&manager->new_mapped_toplevel);

    return true;
}
