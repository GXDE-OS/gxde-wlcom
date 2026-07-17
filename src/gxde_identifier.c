/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This file is part of gxde-daemon.
 *
 * gxde-daemon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gxde-daemon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gxde-daemon.  If not, see <https://www.gnu.org/licenses/>.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <kywc/log.h>

#include "gxde_identifier.h"
#include "server.h"
#include "gxde-identifier-v1-protocol.h"

struct gxde_identifier {
    struct wl_global *global;
    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

static void identifier_handle_destroy(struct wl_client *client,
        struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static const struct gxde_identifier_v1_interface identifier_impl = {
    .destroy = identifier_handle_destroy,
};

static void gxde_identifier_bind(struct wl_client *client, void *data,
        uint32_t version, uint32_t id) {
    struct wl_resource *resource =
        wl_resource_create(client, &gxde_identifier_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &identifier_impl, NULL, NULL);

    /* Send the WM version immediately upon binding */
    gxde_identifier_v1_send_version(resource, KYWC_VERSION);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
    struct gxde_identifier *identifier =
        wl_container_of(listener, identifier, display_destroy);
    wl_list_remove(&identifier->display_destroy.link);
    wl_list_remove(&identifier->server_destroy.link);
    free(identifier);
}

static void handle_server_destroy(struct wl_listener *listener, void *data) {
    struct gxde_identifier *identifier =
        wl_container_of(listener, identifier, server_destroy);
    wl_global_destroy(identifier->global);
}

bool gxde_identifier_create(struct server *server) {
    struct gxde_identifier *identifier = calloc(1, sizeof(*identifier));
    if (!identifier) {
        return false;
    }

    identifier->global = wl_global_create(server->display, &gxde_identifier_v1_interface, 1,
                                          identifier, gxde_identifier_bind);
    if (!identifier->global) {
        kywc_log(KYWC_WARN, "(Identifer) Init: Creation failed!");
        free(identifier);
        return false;
    }

    identifier->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &identifier->display_destroy);
    identifier->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &identifier->server_destroy);

    kywc_log(KYWC_INFO, "(Identifer) Init: Created w/ version %s.", KYWC_VERSION);
    return true;
}
