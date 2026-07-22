/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This file is part of gxde-wlcom.
 *
 * gxde-wlcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gxde-wlcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gxde-wlcom.  If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------
 * The ported implementation for Treeland's treeland-app-id-resolver-v1.
 * Thanks to treeland. This implementation is based on their solution.
 */

#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <wayland-server-core.h>

#include <kywc/log.h>

#include "app_id_resolver.h"
#include "server.h"
#include "treeland-app-id-resolver-v1-protocol.h"

struct app_id_resolver {
    struct wl_resource *resource;
    struct app_id_resolver_manager *manager;
    uint32_t next_request_id;
};

struct app_id_resolver_request {
    struct wl_list link;
    uint32_t id;
    app_id_resolver_cb callback;
    void *user_data;
};

struct app_id_resolver_manager {
    struct wl_global *global;
    struct app_id_resolver *resolver;
    struct wl_list pending;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

static struct app_id_resolver_manager *g_manager = NULL;

/* The resolver object */
static void flush_pending(struct app_id_resolver_manager *manager, const char *app_id) {
    struct app_id_resolver_request *req, *tmp;
    wl_list_for_each_safe(req, tmp, &manager->pending, link) {
        wl_list_remove(&req->link);
        if (req->callback) {
            req->callback(app_id, req->user_data);
        }

        free(req);
    }
}

static uint32_t resolver_request_resolve(struct app_id_resolver *resolver, int pidfd) {
    if (!resolver || pidfd < 0) {
        return 0;
    }

    int dupfd = fcntl(pidfd, F_DUPFD_CLOEXEC, 0);
    if (dupfd < 0) {
        return 0;
    }

    uint32_t id = resolver->next_request_id++;
    /* libwayland dups the fd into the outgoing message, closes dupfd on flush */
    treeland_app_id_resolver_v1_send_identify_request(resolver->resource, id, dupfd);
    return id;
}

static void resolver_handle_respond(struct wl_client *client, struct wl_resource *resource,
        uint32_t request_id, const char *app_id, const char *sandbox_engine_name) {
    /* sandbox_engine_name is accepted but currently unused, matching Treeland */
    (void)client;
    (void)sandbox_engine_name;

    struct app_id_resolver *resolver = wl_resource_get_user_data(resource);
    if (!resolver || !resolver->manager) {
        return;
    }
    struct app_id_resolver_manager *manager = resolver->manager;

    struct app_id_resolver_request *req, *tmp;
    wl_list_for_each_safe(req, tmp, &manager->pending, link) {
        if (req->id != request_id) {
            continue;
        }
        wl_list_remove(&req->link);
        if (req->callback) {
            req->callback(app_id ? app_id : "", req->user_data);
        }
        free(req);
        return;
    }
}

static void resolver_handle_destroy(struct wl_client *client, struct wl_resource *resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static const struct treeland_app_id_resolver_v1_interface resolver_impl = {
    .respond = resolver_handle_respond,
    .destroy = resolver_handle_destroy,
};

static void resolver_handle_resource_destroy(struct wl_resource *resource) {
    struct app_id_resolver *resolver = wl_resource_get_user_data(resource);
    if (!resolver) {
        return;
    }
    struct app_id_resolver_manager *manager = resolver->manager;

    if (manager) {
        manager->resolver = NULL;
        flush_pending(manager, "");
    }
    free(resolver);

    kywc_log(KYWC_INFO, "(AppIdResolver) Resolver disconnected");
}

/* Manager object */
static void manager_handle_get_resolver(struct wl_client *client, struct wl_resource *resource,
        uint32_t id) {
    struct app_id_resolver_manager *manager = wl_resource_get_user_data(resource);

    if (manager->resolver) {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "resolver already exists");
        return;
    }

    struct app_id_resolver *resolver = calloc(1, sizeof(*resolver));
    if (!resolver) {
        wl_client_post_no_memory(client);
        return;
    }

    struct wl_resource *resolver_resource = wl_resource_create(
        client, &treeland_app_id_resolver_v1_interface, wl_resource_get_version(resource), id);
    if (!resolver_resource) {
        free(resolver);
        wl_client_post_no_memory(client);
        return;
    }

    resolver->resource = resolver_resource;
    resolver->manager = manager;
    resolver->next_request_id = 1;

    wl_resource_set_implementation(resolver_resource, &resolver_impl, resolver,
                                   resolver_handle_resource_destroy);

    manager->resolver = resolver;
    kywc_log(KYWC_INFO, "(AppIdResolver) Resolver bound");
}

static void manager_handle_destroy(struct wl_client *client, struct wl_resource *resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static const struct treeland_app_id_resolver_manager_v1_interface manager_impl = {
    .get_resolver = manager_handle_get_resolver,
    .destroy = manager_handle_destroy,
};

static void manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct app_id_resolver_manager *manager = data;

    struct wl_resource *resource =
        wl_resource_create(client, &treeland_app_id_resolver_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &manager_impl, manager, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct app_id_resolver_manager *manager =
        wl_container_of(listener, manager, display_destroy);
    wl_list_remove(&manager->display_destroy.link);
    wl_list_remove(&manager->server_destroy.link);

    if (manager->resolver) {
        manager->resolver->manager = NULL;
        manager->resolver = NULL;
    }
    struct app_id_resolver_request *req, *tmp;
    wl_list_for_each_safe(req, tmp, &manager->pending, link) {
        wl_list_remove(&req->link);
        free(req);
    }

    if (g_manager == manager) {
        g_manager = NULL;
    }
    free(manager);
}

static void handle_server_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct app_id_resolver_manager *manager =
        wl_container_of(listener, manager, server_destroy);
    wl_global_destroy(manager->global);
}

/* Public API */

struct app_id_resolver_manager *app_id_resolver_manager_create(struct server *server) {
    struct app_id_resolver_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return NULL;
    }

    /* Fully initialise state before publishing the global. */
    wl_list_init(&manager->pending);

    manager->global = wl_global_create(server->display,
                                       &treeland_app_id_resolver_manager_v1_interface, 1, manager,
                                       manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_WARN, "(AppIdResolver) Init: Creation failed!");
        free(manager);
        return NULL;
    }

    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);
    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);

    g_manager = manager;
    kywc_log(KYWC_INFO, "(AppIdResolver) Init: Created.");
    return manager;
}

struct app_id_resolver_manager *app_id_resolver_manager_get(void) {
    return g_manager;
}

bool app_id_resolver_manager_available(struct app_id_resolver_manager *manager) {
    return manager && manager->resolver;
}

bool app_id_resolver_manager_resolve_pidfd(struct app_id_resolver_manager *manager, int pidfd,
        app_id_resolver_cb callback, void *user_data) {
    if (!manager || !manager->resolver) {
        return false;
    }

    uint32_t id = resolver_request_resolve(manager->resolver, pidfd);
    if (id == 0) {
        return false;
    }

    struct app_id_resolver_request *req = calloc(1, sizeof(*req));
    if (!req) {
        return false;
    }
    req->id = id;
    req->callback = callback;
    req->user_data = user_data;
    wl_list_insert(&manager->pending, &req->link);
    return true;
}
