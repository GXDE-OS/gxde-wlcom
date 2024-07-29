// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_region.h>

#include "kywc/log.h"
#include "scene/surface.h"
#include "ukui-blur-v1-protocol.h"
#include "view_p.h"

#define UKUI_BLUR_MANAGER_VERSION 1
#define UKUI_BLUR_SURFACE_VERSION 1

enum UKUI_BLUR_STATE_MASK {
    UKUI_BLUR_STATE_NONE = 0,
    UKUI_BLUR_STATE_REGION = 1 << 0,
    UKUI_BLUR_STATE_LEVEL = 1 << 1,
};

struct ukui_blur_manager {
    struct wl_global *global;
    struct wl_list ukui_blur_surfaces;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct ukui_blur_surface {
    struct wl_list link;
    struct wl_resource *resource;

    struct wlr_surface *wlr_surface;
    struct wl_listener surface_commit;
    struct wl_listener surface_map;
    struct wl_listener surface_destroy;

    struct ky_scene_buffer *scene_buffer;
    struct wl_listener node_destroy;

    pixman_region32_t region, pending_region;
    uint32_t level, pending_level;
    uint32_t pending_mask;
};

static struct ukui_blur_manager *manager = NULL;

struct blur_level {
    int iterations;
    float offset;
};

static const struct blur_level blur_levels[] = {
    { 1, 1.5 },     { 1, 2 },   { 2, 2.5 },     { 2, 3.0 },     { 3, 2.6 },
    { 3, 3.2 },     { 3, 3.8 }, { 3, 4.4 },     { 3, 5 },       { 4, 3.83333 },
    { 4, 4.66667 }, { 4, 5.5 }, { 4, 6.33333 }, { 4, 7.16667 }, { 4, 8 },
};

static void blur_surface_apply_state(struct ukui_blur_surface *blur_surface)
{
    /* the level is five, if not set. iterations = 3, offset = 2.6 */
    if (blur_surface->pending_mask & UKUI_BLUR_STATE_REGION) {
        ky_scene_node_set_blur_region(&blur_surface->scene_buffer->node,
                                      &blur_surface->pending_region);
        blur_surface->region = blur_surface->pending_region;
    }
    if (blur_surface->pending_mask & UKUI_BLUR_STATE_LEVEL) {
        const struct blur_level *level = &blur_levels[blur_surface->pending_level - 1];
        ky_scene_node_set_blur_level(&blur_surface->scene_buffer->node, level->iterations,
                                     level->offset);
        blur_surface->level = blur_surface->pending_level;
    }

    blur_surface->pending_mask = UKUI_BLUR_STATE_NONE;
}

static void blur_surface_destroy(struct ukui_blur_surface *blur_surface)
{
    /* remove blur if surface is not destroyed */
    if (blur_surface->scene_buffer) {
        ky_scene_node_set_blur_region(&blur_surface->scene_buffer->node, NULL);
    }
    /* clear destructor when surface destroyed before blur resources */
    wl_resource_set_user_data(blur_surface->resource, NULL);
    wl_resource_set_destructor(blur_surface->resource, NULL);

    wl_list_remove(&blur_surface->link);
    wl_list_remove(&blur_surface->surface_commit.link);
    wl_list_remove(&blur_surface->surface_map.link);
    wl_list_remove(&blur_surface->surface_destroy.link);
    wl_list_remove(&blur_surface->node_destroy.link);
    pixman_region32_fini(&blur_surface->region);
    pixman_region32_fini(&blur_surface->pending_region);
    free(blur_surface);
}

static void blur_surface_handle_node_destroy(struct wl_listener *listener, void *data)
{
    struct ukui_blur_surface *blur_surface = wl_container_of(listener, blur_surface, node_destroy);
    blur_surface->scene_buffer = NULL;
    blur_surface_destroy(blur_surface);
}

static void blur_surface_handle_surface_map(struct wl_listener *listener, void *data)
{
    struct ukui_blur_surface *blur_surface = wl_container_of(listener, blur_surface, surface_map);

    blur_surface->scene_buffer = ky_scene_buffer_try_from_surface(blur_surface->wlr_surface);
    wl_signal_add(&blur_surface->scene_buffer->node.events.destroy, &blur_surface->node_destroy);
}

static void blur_surface_handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct ukui_blur_surface *blur_surface =
        wl_container_of(listener, blur_surface, surface_destroy);
    blur_surface_destroy(blur_surface);
}

static void blur_surface_handle_surface_commit(struct wl_listener *listener, void *data)
{
    struct ukui_blur_surface *blur_surface =
        wl_container_of(listener, blur_surface, surface_commit);

    if (!blur_surface->wlr_surface->mapped) {
        return;
    }

    if (blur_surface->pending_mask != UKUI_BLUR_STATE_NONE) {
        blur_surface_apply_state(blur_surface);
    }
}

static void ukui_blur_surface_handle_resource_destroy(struct wl_resource *resource)
{
    struct ukui_blur_surface *blur_surface = wl_resource_get_user_data(resource);
    blur_surface_destroy(blur_surface);
}

static void ukui_blur_surface_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void ukui_blur_surface_handle_set_region(struct wl_client *client,
                                                struct wl_resource *resource,
                                                struct wl_resource *region_resource)
{
    struct ukui_blur_surface *blur_surface = wl_resource_get_user_data(resource);
    if (!blur_surface) {
        return;
    }

    if (region_resource) {
        const pixman_region32_t *region = wlr_region_from_resource(region_resource);
        pixman_region32_copy(&blur_surface->pending_region, region);
    } else {
        pixman_region32_clear(&blur_surface->pending_region);
    }

    if (!pixman_region32_equal(&blur_surface->pending_region, &blur_surface->region)) {
        blur_surface->pending_mask |= UKUI_BLUR_STATE_REGION;
    }
}

static void ukui_blur_surface_handle_set_level(struct wl_client *client,
                                               struct wl_resource *resource, uint32_t level)
{
    struct ukui_blur_surface *blur_surface = wl_resource_get_user_data(resource);
    if (!blur_surface) {
        return;
    }

    if (level < 1 || level > 15) {
        kywc_log(KYWC_DEBUG, "ukui blur level is invalid");
        return;
    }

    blur_surface->pending_level = level;
    if (blur_surface->pending_level != blur_surface->level) {
        blur_surface->pending_mask |= UKUI_BLUR_STATE_LEVEL;
    }
}

static const struct ukui_blur_surface_v1_interface ukui_blur_surface_impl = {
    .destroy = ukui_blur_surface_handle_destroy,
    .set_region = ukui_blur_surface_handle_set_region,
    .set_level = ukui_blur_surface_handle_set_level,
};

static struct ukui_blur_surface *blur_surface_from_wlr_surface(struct wlr_surface *wlr_surface)
{
    struct ukui_blur_surface *blur_surface, *tmp;
    wl_list_for_each_safe(blur_surface, tmp, &manager->ukui_blur_surfaces, link) {
        if (blur_surface->wlr_surface == wlr_surface) {
            return blur_surface;
        }
    }
    return NULL;
}

static void handle_blur_manager_get_blur(struct wl_client *client,
                                         struct wl_resource *manager_resource, uint32_t id,
                                         struct wl_resource *surface_resource)
{
    struct wlr_surface *wlr_surface = wlr_surface_from_resource(surface_resource);
    if (!wlr_surface) {
        return;
    }

    struct ukui_blur_surface *blur_surface = blur_surface_from_wlr_surface(wlr_surface);
    if (blur_surface) {
        wl_resource_post_error(manager_resource, UKUI_BLUR_MANAGER_V1_ERROR_BLUR_EXISTS,
                               "ukui blur surface already exists for this surface");
        return;
    }

    blur_surface = calloc(1, sizeof(struct ukui_blur_surface));
    if (!blur_surface) {
        wl_resource_post_no_memory(manager_resource);
        return;
    }

    wl_list_insert(&manager->ukui_blur_surfaces, &blur_surface->link);

    pixman_region32_init(&blur_surface->region);
    pixman_region32_init(&blur_surface->pending_region);

    blur_surface->wlr_surface = wlr_surface;
    blur_surface->surface_map.notify = blur_surface_handle_surface_map;
    wl_signal_add(&wlr_surface->events.map, &blur_surface->surface_map);
    blur_surface->surface_destroy.notify = blur_surface_handle_surface_destroy;
    wl_signal_add(&wlr_surface->events.destroy, &blur_surface->surface_destroy);
    blur_surface->surface_commit.notify = blur_surface_handle_surface_commit;
    wl_signal_add(&wlr_surface->events.commit, &blur_surface->surface_commit);
    blur_surface->node_destroy.notify = blur_surface_handle_node_destroy;
    wl_list_init(&blur_surface->node_destroy.link);

    struct wl_resource *resource =
        wl_resource_create(client, &ukui_blur_surface_v1_interface, UKUI_BLUR_SURFACE_VERSION, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &ukui_blur_surface_impl, blur_surface,
                                   ukui_blur_surface_handle_resource_destroy);
    blur_surface->resource = resource;
}

static void handle_blur_manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct ukui_blur_manager_v1_interface ukui_blur_manager_v1_impl = {
    .destroy = handle_blur_manager_destroy,
    .get_blur = handle_blur_manager_get_blur,
};

static void ukui_blur_manager_bind(struct wl_client *client, void *data, uint32_t version,
                                   uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &ukui_blur_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &ukui_blur_manager_v1_impl, manager, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->display_destroy.link);
    wl_global_destroy(manager->global);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
    manager = NULL;
}

bool ukui_blur_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(struct ukui_blur_manager));
    if (!manager) {
        return false;
    }

    manager->global = wl_global_create(server->display, &ukui_blur_manager_v1_interface,
                                       UKUI_BLUR_MANAGER_VERSION, manager, ukui_blur_manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_WARN, "ukui blur manager create failed");
        free(manager);
        return false;
    }

    wl_list_init(&manager->ukui_blur_surfaces);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    return false;
}
