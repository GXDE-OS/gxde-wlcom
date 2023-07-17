#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_region.h>

#include "blur-protocol.h"
#include "view_p.h"

#define KDE_KWIN_BLUR_MANAGER_VERSION 1
#define KDE_KWIN_BLUR_VERSION 1

struct kde_blur_manager {
    struct wl_global *global;
    struct wl_list kde_blurs;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct kde_blur {
    struct wl_list link;
    struct kde_blur_manager *manager;

    struct wlr_surface *wlr_surface;

    pixman_region32_t region, pending_region;
    uint32_t strength, pending_strength;
};

static struct kde_blur *kde_blur_from_wlr_surface(struct kde_blur_manager *manager,
                                                  struct wlr_surface *wlr_surface)
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
    pixman_region32_copy(&blur->region, &blur->pending_region);
    blur->strength = blur->pending_strength;
}

static void kde_blur_handle_set_region(struct wl_client *client, struct wl_resource *resource,
                                       struct wl_resource *region_resource)
{
    struct kde_blur *blur = wl_resource_get_user_data(resource);

    if (region_resource) {
        const pixman_region32_t *region = wlr_region_from_resource(region_resource);
        pixman_region32_copy(&blur->pending_region, region);
    } else {
        pixman_region32_clear(&blur->pending_region);
    }
}

static void kde_blur_handle_set_strength(struct wl_client *client, struct wl_resource *resource,
                                         uint32_t strength)
{
    struct kde_blur *blur = wl_resource_get_user_data(resource);
    blur->pending_strength = strength;
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
    wl_list_remove(&blur->link);
    pixman_region32_fini(&blur->region);
    pixman_region32_fini(&blur->pending_region);
    free(blur);
}

static void kde_blur_handle_resource_destroy(struct wl_resource *resource)
{
    struct kde_blur *blur = wl_resource_get_user_data(resource);
    kde_blur_destroy(blur);
}

static void handle_create(struct wl_client *client, struct wl_resource *manager_resource,
                          uint32_t id, struct wl_resource *surface_resource)
{
    struct wlr_surface *wlr_surface = wlr_surface_from_resource(surface_resource);
    if (!wlr_surface) {
        return;
    }

    struct kde_blur *blur = calloc(1, sizeof(struct kde_blur));
    if (!blur) {
        return;
    }

    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_kwin_blur_interface, KDE_KWIN_BLUR_VERSION, id);
    if (!resource) {
        free(blur);
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &kde_blur_impl, blur,
                                   kde_blur_handle_resource_destroy);

    struct kde_blur_manager *manager = wl_resource_get_user_data(manager_resource);
    wl_list_insert(&manager->kde_blurs, &blur->link);

    pixman_region32_init(&blur->region);
    pixman_region32_init(&blur->pending_region);

    blur->wlr_surface = wlr_surface;
}

static void handle_unset(struct wl_client *client, struct wl_resource *manager_resource,
                         struct wl_resource *surface_resource)
{
    struct wlr_surface *wlr_surface = wlr_surface_from_resource(surface_resource);
    if (!wlr_surface) {
        return;
    }

    struct kde_blur_manager *manager = wl_resource_get_user_data(manager_resource);
    struct kde_blur *blur = kde_blur_from_wlr_surface(manager, wlr_surface);
    if (!blur) {
        return;
    }

    kde_blur_destroy(blur);
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

    struct kde_blur_manager *manager = data;
    wl_resource_set_implementation(resource, &kde_blur_manager_impl, manager, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct kde_blur_manager *manager = wl_container_of(listener, manager, display_destroy);
    wl_list_remove(&manager->display_destroy.link);
    wl_global_destroy(manager->global);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct kde_blur_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
}

bool kde_blur_manager_create(struct server *server)
{
    struct kde_blur_manager *manager = calloc(1, sizeof(struct kde_blur_manager));
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

    return false;
}
