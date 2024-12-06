// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <kywc/log.h>

#include "xdg-toplevel-drag-v1-protocol.h"

#include "input_p.h"
#include "server.h"
#include "view/view.h"

struct toplevel_drag_manager {
    struct wl_global *global;
    struct wl_list drags;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct toplevel_drag {
    struct wl_resource *resource;
    struct wl_list link;

    struct wlr_data_source *source;
    struct wl_listener source_destroy;

    struct wlr_xdg_toplevel *toplevel;
    struct wl_listener toplevel_unmap;
    struct wl_listener toplevel_destroy;

    int32_t off_x, off_y;
};

static struct toplevel_drag_manager *manager = NULL;

static struct view *toplevel_set_input_bypassed(struct wlr_xdg_toplevel *toplevel, bool bypassed)
{
    if (!toplevel) {
        return NULL;
    }

    struct view *view = view_try_from_wlr_surface(toplevel->base->surface);
    if (!view) {
        return NULL;
    }

    ky_scene_node_set_input_bypassed(&view->tree->node, bypassed);
    return view;
}

static void toplevel_drag_destroy(struct toplevel_drag *drag)
{
    toplevel_set_input_bypassed(drag->toplevel, false);

    wl_list_remove(&drag->source_destroy.link);
    wl_list_remove(&drag->toplevel_unmap.link);
    wl_list_remove(&drag->toplevel_destroy.link);
    wl_list_remove(&drag->link);

    free(drag);
}

static void toplevel_drag_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void toplevel_drag_handle_attach(struct wl_client *client, struct wl_resource *resource,
                                        struct wl_resource *toplevel, int32_t x_offset,
                                        int32_t y_offset)
{
    struct toplevel_drag *drag = wl_resource_get_user_data(resource);
    if (!drag) {
        return;
    }

    if (drag->toplevel) {
        wl_resource_post_error(resource, XDG_TOPLEVEL_DRAG_V1_ERROR_TOPLEVEL_ATTACHED,
                               "valid toplevel already attached");
        return;
    }

    drag->toplevel = wlr_xdg_toplevel_from_resource(toplevel);
    if (!drag->toplevel) {
        return;
    }

    wl_list_remove(&drag->toplevel_unmap.link);
    wl_signal_add(&drag->toplevel->base->surface->events.unmap, &drag->toplevel_unmap);
    wl_list_remove(&drag->toplevel_destroy.link);
    wl_signal_add(&drag->toplevel->base->events.destroy, &drag->toplevel_destroy);

    drag->off_x = x_offset;
    drag->off_y = y_offset;
}

static const struct xdg_toplevel_drag_v1_interface toplevel_drag_impl = {
    .destroy = toplevel_drag_handle_destroy,
    .attach = toplevel_drag_handle_attach,
};

static void toplevel_drag_handle_resource_destroy(struct wl_resource *resource)
{
    struct toplevel_drag *drag = wl_resource_get_user_data(resource);
    if (drag) {
        toplevel_drag_destroy(drag);
    }
}

static void toplevel_drag_handle_source_destroy(struct wl_listener *listener, void *data)
{
    struct toplevel_drag *drag = wl_container_of(listener, drag, source_destroy);
    wl_resource_set_user_data(drag->resource, NULL);
    toplevel_drag_destroy(drag);
}

static void toplevel_drag_reset_toplevel(struct toplevel_drag *drag)
{
    wl_list_remove(&drag->toplevel_unmap.link);
    wl_list_remove(&drag->toplevel_destroy.link);
    wl_list_init(&drag->toplevel_unmap.link);
    wl_list_init(&drag->toplevel_destroy.link);

    toplevel_set_input_bypassed(drag->toplevel, false);
    drag->toplevel = NULL;
}

static void toplevel_drag_handle_toplevel_unmap(struct wl_listener *listener, void *data)
{
    struct toplevel_drag *drag = wl_container_of(listener, drag, toplevel_unmap);
    toplevel_drag_reset_toplevel(drag);
}

static void toplevel_drag_handle_toplevel_destroy(struct wl_listener *listener, void *data)
{
    struct toplevel_drag *drag = wl_container_of(listener, drag, toplevel_destroy);
    toplevel_drag_reset_toplevel(drag);
}

static void manager_handle_get_toplevel_drag(struct wl_client *client, struct wl_resource *resource,
                                             uint32_t id, struct wl_resource *data_source)
{
    // trick to get wlr_data_source as wlr_client_data_source is private
    struct wlr_data_source *source = wl_resource_get_user_data(data_source);
    if (!source) {
        wl_client_post_implementation_error(client, "invalid data source");
        return;
    }

    struct toplevel_drag *drag = calloc(1, sizeof(*drag));
    if (!drag) {
        wl_client_post_no_memory(client);
        return;
    }

    int version = wl_resource_get_version(resource);
    drag->resource = wl_resource_create(client, &xdg_toplevel_drag_v1_interface, version, id);
    if (!drag->resource) {
        free(drag);
        wl_client_post_no_memory(client);
        return;
    }

    wl_list_insert(&manager->drags, &drag->link);
    wl_resource_set_implementation(drag->resource, &toplevel_drag_impl, drag,
                                   toplevel_drag_handle_resource_destroy);

    drag->source = source;
    drag->source_destroy.notify = toplevel_drag_handle_source_destroy;
    wl_signal_add(&source->events.destroy, &drag->source_destroy);

    drag->toplevel_unmap.notify = toplevel_drag_handle_toplevel_unmap;
    wl_list_init(&drag->toplevel_unmap.link);
    drag->toplevel_destroy.notify = toplevel_drag_handle_toplevel_destroy;
    wl_list_init(&drag->toplevel_destroy.link);
}

static void manager_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct xdg_toplevel_drag_manager_v1_interface toplevel_drag_manager_impl = {
    .destroy = manager_handle_destroy,
    .get_xdg_toplevel_drag = manager_handle_get_toplevel_drag,
};

static void toplevel_drag_manager_bind(struct wl_client *client, void *data, uint32_t version,
                                       uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &xdg_toplevel_drag_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &toplevel_drag_manager_impl, manager, NULL);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
    manager = NULL;
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->display_destroy.link);
    wl_global_destroy(manager->global);
}

bool toplevel_drag_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    manager->global = wl_global_create(server->display, &xdg_toplevel_drag_manager_v1_interface, 1,
                                       manager, toplevel_drag_manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_WARN, "xdg_toplevel_drag_manager_v1 create failed");
        free(manager);
        manager = NULL;
        return false;
    }

    wl_list_init(&manager->drags);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    return true;
}

static struct toplevel_drag *toplevel_drag_from_data_source(struct wlr_data_source *source)
{
    struct toplevel_drag *drag;
    wl_list_for_each(drag, &manager->drags, link) {
        if (drag->source == source) {
            return drag;
        }
    }
    return NULL;
}

void toplevel_drag_move(struct wlr_data_source *source, int lx, int ly)
{
    if (!manager || !source) {
        return;
    }

    struct toplevel_drag *drag = toplevel_drag_from_data_source(source);
    if (!drag || !drag->toplevel) {
        return;
    }

    struct view *view = toplevel_set_input_bypassed(drag->toplevel, true);
    if (view) {
        view_do_move(view, lx - drag->off_x, ly - drag->off_y);
    }
}
