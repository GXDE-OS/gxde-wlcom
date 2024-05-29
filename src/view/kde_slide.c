// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_compositor.h>

#include "scene/surface.h"
#include "slide-protocol.h"
#include "view_p.h"

#define KDE_KWIN_SLIDE_MANAGER_VERSION 1
#define KDE_KWIN_SLIDE_VERSION 1

struct kde_slide_manager {
    struct wl_global *global;
    struct wl_list kde_slides;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct kde_slide {
    struct wl_list link;
    struct wl_list resources;

    struct wlr_surface *wlr_surface;
    struct wl_listener surface_map;
    struct wl_listener surface_destroy;

    uint32_t location, pending_location;
    int32_t offset, pending_offset;
};

static struct kde_slide_manager *manager = NULL;

static void kde_slide_apply_state(struct kde_slide *slide)
{
    slide->offset = slide->pending_offset;
    slide->location = slide->pending_location;

    ky_scene_surface_set_slide(slide->wlr_surface, slide->location, slide->offset);
}

static struct kde_slide *kde_slide_from_wlr_surface(struct wlr_surface *wlr_surface)
{
    struct kde_slide *slide;
    wl_list_for_each(slide, &manager->kde_slides, link) {
        if (slide->wlr_surface == wlr_surface) {
            return slide;
        }
    }
    return NULL;
}

static void kde_slide_handle_commit(struct wl_client *client, struct wl_resource *resource)
{
    struct kde_slide *slide = wl_resource_get_user_data(resource);
    if (!slide) {
        return;
    }

    kde_slide_apply_state(slide);
}

static void kde_slide_handle_set_location(struct wl_client *client, struct wl_resource *resource,
                                          uint32_t location)
{
    struct kde_slide *slide = wl_resource_get_user_data(resource);
    if (!slide) {
        return;
    }

    if (location != slide->location) {
        slide->pending_location = location;
    }
}

static void kde_slide_handle_set_offset(struct wl_client *client, struct wl_resource *resource,
                                        int32_t offset)
{
    struct kde_slide *slide = wl_resource_get_user_data(resource);
    if (!slide) {
        return;
    }

    if (offset != slide->pending_offset) {
        slide->pending_offset = offset;
    }
}

static void kde_slide_handle_release(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct org_kde_kwin_slide_interface kde_slide_impl = {
    .commit = kde_slide_handle_commit,
    .set_location = kde_slide_handle_set_location,
    .set_offset = kde_slide_handle_set_offset,
    .release = kde_slide_handle_release,
};

static void kde_slide_destroy(struct kde_slide *slide)
{
    if (slide->wlr_surface) {
        ky_scene_surface_unset_slide(slide->wlr_surface);
    }

    /* clear destructor when surface destroyed before slide resources */
    struct wl_resource *resource, *tmp;
    wl_resource_for_each_safe(resource, tmp, &slide->resources) {
        wl_resource_set_user_data(resource, NULL);
        wl_resource_set_destructor(resource, NULL);
        wl_list_remove(&resource->link);
        wl_list_init(&resource->link);
    }

    wl_list_remove(&slide->link);
    wl_list_remove(&slide->surface_map.link);
    wl_list_remove(&slide->surface_destroy.link);
    free(slide);
}

static void kde_slide_handle_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(&resource->link);
    /* destroy slide if no slide resource */
    struct kde_slide *slide = wl_resource_get_user_data(resource);
    if (slide && wl_list_empty(&slide->resources)) {
        kde_slide_destroy(slide);
    }
}

static void slide_handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct kde_slide *slide = wl_container_of(listener, slide, surface_destroy);
    slide->wlr_surface = NULL;
    kde_slide_destroy(slide);
}

static void slide_handle_surface_map(struct wl_listener *listener, void *data)
{
    struct kde_slide *slide = wl_container_of(listener, slide, surface_map);
    wl_list_remove(&slide->surface_map.link);
    wl_list_init(&slide->surface_map.link);
    kde_slide_apply_state(slide);
}

static void handle_create(struct wl_client *client, struct wl_resource *manager_resource,
                          uint32_t id, struct wl_resource *surface_resource)
{
    struct wlr_surface *wlr_surface = wlr_surface_from_resource(surface_resource);
    if (!wlr_surface) {
        return;
    }

    struct kde_slide *slide = kde_slide_from_wlr_surface(wlr_surface);
    if (!slide) {
        slide = calloc(1, sizeof(struct kde_slide));
        if (!slide) {
            return;
        }

        wl_list_init(&slide->resources);
        wl_list_insert(&manager->kde_slides, &slide->link);

        slide->wlr_surface = wlr_surface;
        slide->surface_map.notify = slide_handle_surface_map;
        slide->surface_destroy.notify = slide_handle_surface_destroy;
        wl_signal_add(&wlr_surface->events.destroy, &slide->surface_destroy);

        if (wlr_surface->mapped) {
            wl_list_init(&slide->surface_map.link);
        } else {
            wl_signal_add(&wlr_surface->events.map, &slide->surface_map);
        }
    }

    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_kwin_slide_interface, KDE_KWIN_SLIDE_VERSION, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_list_insert(&slide->resources, &resource->link);
    wl_resource_set_implementation(resource, &kde_slide_impl, slide,
                                   kde_slide_handle_resource_destroy);
}

static void handle_unset(struct wl_client *client, struct wl_resource *manager_resource,
                         struct wl_resource *surface_resource)
{
    struct wlr_surface *wlr_surface = wlr_surface_from_resource(surface_resource);
    if (!wlr_surface) {
        return;
    }

    struct kde_slide *slide = kde_slide_from_wlr_surface(wlr_surface);
    if (slide) {
        kde_slide_destroy(slide);
    }
}

static const struct org_kde_kwin_slide_manager_interface kde_slide_manager_impl = {
    .create = handle_create,
    .unset = handle_unset,
};

static void kde_slide_manager_bind(struct wl_client *client, void *data, uint32_t version,
                                   uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_kwin_slide_manager_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &kde_slide_manager_impl, manager, NULL);
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

bool kde_slide_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(struct kde_slide_manager));
    if (!manager) {
        return false;
    }

    manager->global =
        wl_global_create(server->display, &org_kde_kwin_slide_manager_interface,
                         KDE_KWIN_SLIDE_MANAGER_VERSION, manager, kde_slide_manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_WARN, "kde slide manager create failed");
        free(manager);
        return false;
    }

    wl_list_init(&manager->kde_slides);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    return false;
}
