// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_region.h>

#include "blur-protocol.h"
#include "scene/surface.h"
#include "view_p.h"

#define KDE_KWIN_BLUR_MANAGER_VERSION 1

enum KDE_BLUR_STATE_MASK {
    KDE_BLUR_STATE_NONE = 0,
    KDE_BLUR_STATE_REGION = 1 << 0,
    KDE_BLUR_STATE_STRENGTH = 1 << 1,
};

struct kde_blur_manager {
    struct wl_global *global;
    struct wl_list kde_blurs;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct kde_blur {
    struct wl_list link;
    struct wl_list resources;

    struct wlr_surface *wlr_surface;
    struct wl_listener surface_map;
    struct wl_listener surface_destroy;

    struct ky_scene_buffer *scene_buffer;
    struct wl_listener node_destroy;

    pixman_region32_t region, pending_region;
    uint32_t strength, pending_strength;
    uint32_t pending_mask;
};

static struct kde_blur_manager *manager = NULL;

static void kde_blur_apply_state(struct kde_blur *blur)
{
    if (!blur->scene_buffer) {
        return;
    }
    if (blur->pending_mask & KDE_BLUR_STATE_REGION) {
        ky_scene_node_set_blur_region(&blur->scene_buffer->node, &blur->region);
    }
    if (blur->pending_mask & KDE_BLUR_STATE_STRENGTH) {
        float offset = (int)blur->strength == -1 ? 2.6f : blur->strength / 1000.f;
        ky_scene_node_set_blur_level(&blur->scene_buffer->node, 3, offset);
    }

    blur->pending_mask = KDE_BLUR_STATE_NONE;
}

static struct kde_blur *kde_blur_from_wlr_surface(struct wlr_surface *wlr_surface)
{
    struct kde_blur *blur;
    wl_list_for_each(blur, &manager->kde_blurs, link) {
        if (blur->wlr_surface == wlr_surface) {
            return blur;
        }
    }
    return NULL;
}

static void kde_blur_handle_commit(struct wl_client *client, struct wl_resource *resource)
{
    struct kde_blur *blur = wl_resource_get_user_data(resource);
    if (!blur) {
        return;
    }

    pixman_region32_copy(&blur->region, &blur->pending_region);
    blur->strength = blur->pending_strength;

    if (!blur->wlr_surface->mapped) {
        return;
    }

    kde_blur_apply_state(blur);
}

static void kde_blur_handle_set_region(struct wl_client *client, struct wl_resource *resource,
                                       struct wl_resource *region_resource)
{
    struct kde_blur *blur = wl_resource_get_user_data(resource);
    if (!blur) {
        return;
    }

    if (region_resource) {
        const pixman_region32_t *region = wlr_region_from_resource(region_resource);
        pixman_region32_copy(&blur->pending_region, region);
    } else {
        pixman_region32_clear(&blur->pending_region);
    }

    blur->pending_mask |= KDE_BLUR_STATE_REGION;
}

static void kde_blur_handle_set_strength(struct wl_client *client, struct wl_resource *resource,
                                         uint32_t strength)
{
    struct kde_blur *blur = wl_resource_get_user_data(resource);
    if (!blur) {
        return;
    }

    blur->pending_strength = strength;
    blur->pending_mask |= KDE_BLUR_STATE_STRENGTH;
}

static void kde_blur_handle_release(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct org_kde_kwin_blur_interface kde_blur_impl = {
    .commit = kde_blur_handle_commit,
    .set_region = kde_blur_handle_set_region,
    .set_strength = kde_blur_handle_set_strength,
    .release = kde_blur_handle_release,
};

static void kde_blur_destroy(struct kde_blur *blur)
{
    /* remove blur if surface is not destroyed */
    if (blur->scene_buffer) {
        ky_scene_node_set_blur_region(&blur->scene_buffer->node, NULL);
    }
    /* clear destructor when surface destroyed before blur resources */
    struct wl_resource *resource, *tmp;
    wl_resource_for_each_safe(resource, tmp, &blur->resources) {
        wl_resource_set_user_data(resource, NULL);
        wl_resource_set_destructor(resource, NULL);
        wl_list_remove(wl_resource_get_link(resource));
        wl_list_init(wl_resource_get_link(resource));
    }
    wl_list_remove(&blur->link);
    wl_list_remove(&blur->surface_map.link);
    wl_list_remove(&blur->surface_destroy.link);
    wl_list_remove(&blur->node_destroy.link);
    pixman_region32_fini(&blur->region);
    pixman_region32_fini(&blur->pending_region);
    free(blur);
}

static void kde_blur_handle_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
    /* destroy blur if no blur resource */
    struct kde_blur *blur = wl_resource_get_user_data(resource);
    if (blur && wl_list_empty(&blur->resources)) {
        kde_blur_destroy(blur);
    }
}

static void blur_handle_node_destroy(struct wl_listener *listener, void *data)
{
    struct kde_blur *blur = wl_container_of(listener, blur, node_destroy);
    blur->scene_buffer = NULL;
    kde_blur_destroy(blur);
}

static void blur_handle_surface_map(struct wl_listener *listener, void *data)
{
    struct kde_blur *blur = wl_container_of(listener, blur, surface_map);
    wl_list_remove(&blur->surface_map.link);
    wl_list_init(&blur->surface_map.link);

    blur->scene_buffer = ky_scene_buffer_try_from_surface(blur->wlr_surface);
    if (blur->scene_buffer) {
        wl_signal_add(&blur->scene_buffer->node.events.destroy, &blur->node_destroy);
    }

    kde_blur_apply_state(blur);
}

static void blur_handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct kde_blur *blur = wl_container_of(listener, blur, surface_destroy);
    kde_blur_destroy(blur);
}

static void handle_create(struct wl_client *client, struct wl_resource *manager_resource,
                          uint32_t id, struct wl_resource *surface_resource)
{
    struct wlr_surface *wlr_surface = wlr_surface_from_resource(surface_resource);
    if (!wlr_surface) {
        return;
    }

    struct kde_blur *blur = kde_blur_from_wlr_surface(wlr_surface);
    if (!blur) {
        blur = calloc(1, sizeof(struct kde_blur));
        if (!blur) {
            return;
        }

        wl_list_init(&blur->resources);
        wl_list_insert(&manager->kde_blurs, &blur->link);

        pixman_region32_init(&blur->region);
        pixman_region32_init(&blur->pending_region);

        blur->wlr_surface = wlr_surface;
        blur->surface_map.notify = blur_handle_surface_map;
        wl_signal_add(&wlr_surface->events.map, &blur->surface_map);
        blur->surface_destroy.notify = blur_handle_surface_destroy;
        wl_signal_add(&wlr_surface->events.destroy, &blur->surface_destroy);
        blur->node_destroy.notify = blur_handle_node_destroy;
        wl_list_init(&blur->node_destroy.link);

        if (wlr_surface->mapped) {
            blur_handle_surface_map(&blur->surface_map, NULL);
        }
    }

    int version = wl_resource_get_version(manager_resource);
    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_kwin_blur_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_list_insert(&blur->resources, wl_resource_get_link(resource));
    wl_resource_set_implementation(resource, &kde_blur_impl, blur,
                                   kde_blur_handle_resource_destroy);
}

static void handle_unset(struct wl_client *client, struct wl_resource *manager_resource,
                         struct wl_resource *surface_resource)
{
    struct wlr_surface *wlr_surface = wlr_surface_from_resource(surface_resource);
    if (!wlr_surface) {
        return;
    }

    struct kde_blur *blur = kde_blur_from_wlr_surface(wlr_surface);
    if (blur) {
        kde_blur_destroy(blur);
    }
}

static const struct org_kde_kwin_blur_manager_interface kde_blur_manager_impl = {
    .create = handle_create,
    .unset = handle_unset,
};

static void kde_blur_manager_bind(struct wl_client *client, void *data, uint32_t version,
                                  uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_kwin_blur_manager_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &kde_blur_manager_impl, manager, NULL);
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

bool kde_blur_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(struct kde_blur_manager));
    if (!manager) {
        return false;
    }

    manager->global =
        wl_global_create(server->display, &org_kde_kwin_blur_manager_interface,
                         KDE_KWIN_BLUR_MANAGER_VERSION, manager, kde_blur_manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_WARN, "kde blur manager create failed");
        free(manager);
        return false;
    }

    wl_list_init(&manager->kde_blurs);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    return true;
}
