// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdio.h>
#include <stdlib.h>

#include <wlr/types/wlr_seat.h>

#include <kywc/log.h>

#include "ext-transient-seat-v1-protocol.h"

#include "input_p.h"
#include "server.h"

struct transient_seat_manager {
    struct wl_global *global;
    struct wl_listener display_destroy;
    struct wl_listener server_destroy;

    struct input_manager *input_manager;
};

struct transient_seat {
    struct wl_resource *resource;
    struct seat *seat;
    struct wl_listener seat_destroy;
};

static void transient_seat_destroy(struct transient_seat *seat)
{
    wl_list_remove(&seat->seat_destroy.link);
    if (seat->seat) {
        seat_destroy(seat->seat);
    }
    free(seat);
}

static void transient_seat_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct ext_transient_seat_v1_interface transient_seat_impl = {
    .destroy = transient_seat_handle_destroy,
};

static void transient_seat_handle_resource_destroy(struct wl_resource *resource)
{
    struct transient_seat *seat = wl_resource_get_user_data(resource);
    if (seat) {
        transient_seat_destroy(seat);
    }
}

static void transient_seat_handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct transient_seat *seat = wl_container_of(listener, seat, seat_destroy);
    wl_resource_set_user_data(seat->resource, NULL);
    seat->seat = NULL;
    transient_seat_destroy(seat);
}

static void manager_handle_create(struct wl_client *client, struct wl_resource *manager_resource,
                                  uint32_t id)
{
    struct transient_seat *seat = calloc(1, sizeof(*seat));
    if (!seat) {
        wl_client_post_no_memory(client);
        return;
    }

    int version = wl_resource_get_version(manager_resource);
    seat->resource = wl_resource_create(client, &ext_transient_seat_v1_interface, version, id);
    if (!seat->resource) {
        free(seat);
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(seat->resource, &transient_seat_impl, seat,
                                   transient_seat_handle_resource_destroy);

    // create seat and send ready or denied
    static uint64_t i = 0;
    char name[256];
    snprintf(name, sizeof(name), "transient-%" PRIx64, i++);

    struct transient_seat_manager *manager = wl_resource_get_user_data(manager_resource);
    seat->seat = seat_create(manager->input_manager, name);
    if (!seat->seat) {
        wl_list_init(&seat->seat_destroy.link);
        ext_transient_seat_v1_send_denied(seat->resource);
        return;
    }

    seat->seat_destroy.notify = transient_seat_handle_seat_destroy;
    wl_signal_add(&seat->seat->events.destroy, &seat->seat_destroy);

    uint32_t global_name = wl_global_get_name(seat->seat->wlr_seat->global, client);
    ext_transient_seat_v1_send_ready(seat->resource, global_name);
}

static void manager_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct ext_transient_seat_manager_v1_interface transient_seat_manager_impl = {
    .create = manager_handle_create,
    .destroy = manager_handle_destroy,
};

static void transient_seat_manager_bind(struct wl_client *client, void *data, uint32_t version,
                                        uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &ext_transient_seat_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    struct transient_seat_manager *manager = data;
    wl_resource_set_implementation(resource, &transient_seat_manager_impl, manager, NULL);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct transient_seat_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct transient_seat_manager *manager = wl_container_of(listener, manager, display_destroy);
    wl_list_remove(&manager->display_destroy.link);
    wl_global_destroy(manager->global);
}

bool transient_seat_manager_create(struct input_manager *input_manager)
{
    struct transient_seat_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    struct server *server = input_manager->server;
    manager->input_manager = input_manager;

    manager->global = wl_global_create(server->display, &ext_transient_seat_manager_v1_interface, 1,
                                       manager, transient_seat_manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_WARN, "ext_transient_seat_manager_v1 create failed");
        free(manager);
        return false;
    }

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    return true;
}
