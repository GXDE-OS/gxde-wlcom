#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_compositor.h>

#include "output.h"
#include "plasma-shell-protocol.h"
#include "view/workspace.h"
#include "view_p.h"

#define PLASMA_SURFACE_VERSION 8
#define PLASMA_SHELL_VERSION 8

struct kde_plasma_shell {
    struct wl_global *global;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct kde_plasma_surface {
    struct kde_plasma_shell *shell;

    struct wlr_surface *wlr_surface;
    struct wl_listener surface_map;

    /* get in map listener */
    struct view *view;
    struct wl_listener view_map;
    struct wl_listener view_unmap;
    struct wl_listener view_minimize;
    struct wl_listener view_size;
    struct wl_listener view_position;
    struct wl_listener view_output;
    struct wl_listener view_destroy;

    struct wl_listener output_update_usable_area;

    int x, y;
    enum org_kde_plasma_surface_role role;
};

static void handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void handle_set_output(struct wl_client *client, struct wl_resource *resource,
                              struct wl_resource *output)
{
    // Not implemented yet
}

static void handle_set_position(struct wl_client *client, struct wl_resource *resource, int32_t x,
                                int32_t y)
{
    struct kde_plasma_surface *surface = wl_resource_get_user_data(resource);

    surface->x = x;
    surface->y = y;

    if (surface->view) {
        kywc_view_move(&surface->view->base, surface->x, surface->y);
    }
}

static void kde_plasma_surface_apply_role(struct kde_plasma_surface *surface)
{
    struct ky_scene_node *node = ky_scene_node_from_tree(surface->view->tree);
    struct view_layer *layer = workspace_layer(surface->view->workspace, LAYER_NORMAL);

    switch (surface->role) {
    case ORG_KDE_PLASMA_SURFACE_ROLE_NORMAL:
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_DESKTOP:
        layer = view_manager_get_layer(LAYER_DESKTOP, false);
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_PANEL:
    case ORG_KDE_PLASMA_SURFACE_ROLE_APPLETPOPUP:
        layer = view_manager_get_layer(LAYER_DOCK, false);
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_TOOLTIP:
        layer = view_manager_get_layer(LAYER_POPUP, false);
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_ONSCREENDISPLAY:
        layer = view_manager_get_layer(LAYER_ON_SCREEN_DISPLAY, false);
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_NOTIFICATION:
        layer = view_manager_get_layer(LAYER_NOTIFICATION, false);
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_CRITICALNOTIFICATION:
        layer = view_manager_get_layer(LAYER_CRITICAL_NOTIFICATION, false);
        break;
    }

    if (surface->role != ORG_KDE_PLASMA_SURFACE_ROLE_NORMAL) {
        view_set_workspace(surface->view, NULL);
        surface->view->base.activatable = false;
    }

    ky_scene_node_reparent(node, layer->tree);
}

static void kde_plasma_surface_set_usable_area(struct kde_plasma_surface *surface, bool enabled);
static void handle_set_role(struct wl_client *client, struct wl_resource *resource, uint32_t role)
{
    struct kde_plasma_surface *surface = wl_resource_get_user_data(resource);

    surface->role = role;

    if (surface->view) {
        kde_plasma_surface_apply_role(surface);
        /* if plasma shell change role after map */
        kde_plasma_surface_set_usable_area(surface, true);
    }
}

static void handle_set_panel_behavior(struct wl_client *client, struct wl_resource *resource,
                                      uint32_t flag)
{
    // Not implemented yet
}

static void handle_set_skip_taskbar(struct wl_client *client, struct wl_resource *resource,
                                    uint32_t skip)
{
    // Not implemented yet
}

static void handle_panel_auto_hide_hide(struct wl_client *client, struct wl_resource *resource)
{
    // Not implemented yet
}

static void handle_panel_auto_hide_show(struct wl_client *client, struct wl_resource *resource)
{
    // Not implemented yet
}

static void handle_set_panel_takes_focus(struct wl_client *client, struct wl_resource *resource,
                                         uint32_t takes_focus)
{
    // Not implemented yet
}

static void handle_set_skip_switcher(struct wl_client *client, struct wl_resource *resource,
                                     uint32_t skip)
{
    // Not implemented yet
}

static void handle_open_under_cursor(struct wl_client *client, struct wl_resource *resource)
{
    // Not implemented yet
}

static const struct org_kde_plasma_surface_interface kde_plasma_surface_impl = {
    .destroy = handle_destroy,
    .set_output = handle_set_output,
    .set_position = handle_set_position,
    .set_role = handle_set_role,
    .set_panel_behavior = handle_set_panel_behavior,
    .set_skip_taskbar = handle_set_skip_taskbar,
    .panel_auto_hide_hide = handle_panel_auto_hide_hide,
    .panel_auto_hide_show = handle_panel_auto_hide_show,
    .set_panel_takes_focus = handle_set_panel_takes_focus,
    .set_skip_switcher = handle_set_skip_switcher,
    .open_under_cursor = handle_open_under_cursor,
};

static void surface_handle_output_update_usable_area(struct wl_listener *listener, void *data)
{
    struct kde_plasma_surface *surface =
        wl_container_of(listener, surface, output_update_usable_area);
    struct kywc_box *usable_area = data;

    struct kywc_box geo;
    kywc_output_effective_geometry(surface->view->output, &geo);

    struct kywc_box *view_geo = &surface->view->base.geometry;
    int mid_x = view_geo->x + view_geo->width / 2;
    int mid_y = view_geo->y + view_geo->height / 2;

    if (view_geo->width > view_geo->height) {
        if (mid_y - geo.y < geo.y + geo.height - mid_y) {
            // position is top
            int y = view_geo->y + view_geo->height;
            geo.height -= y - geo.y;
            geo.y = y;
        } else {
            // position is bottom
            geo.height = view_geo->y - geo.y;
        }
    } else {
        if (mid_x - geo.x < geo.x + geo.width - mid_x) {
            // position is left
            int x = view_geo->width + view_geo->x;
            geo.width -= x - geo.x;
            geo.x = x;
        } else {
            // position is right
            geo.width = view_geo->x - geo.x;
        }
    }

    /* intersect usable_area and geo */
    usable_area->x = geo.x > usable_area->x ? geo.x : usable_area->x;
    usable_area->y = geo.y > usable_area->y ? geo.y : usable_area->y;
    usable_area->width = geo.width < usable_area->width ? geo.width : usable_area->width;
    usable_area->height = geo.height < usable_area->height ? geo.height : usable_area->height;
}

static void kde_plasma_surface_set_usable_area(struct kde_plasma_surface *surface, bool enabled)
{
    bool had_area = !wl_list_empty(&surface->output_update_usable_area.link);
    bool has_area = enabled && surface->role == ORG_KDE_PLASMA_SURFACE_ROLE_PANEL;

    if (!has_area) {
        if (had_area) {
            wl_list_remove(&surface->output_update_usable_area.link);
            wl_list_init(&surface->output_update_usable_area.link);
            kywc_output_update_usable_area(surface->view->output);
        }
        return;
    }

    if (!had_area) {
        surface->output_update_usable_area.notify = surface_handle_output_update_usable_area;
        output_add_update_usable_area_listener(surface->view->output,
                                               &surface->output_update_usable_area, true);
    }

    kywc_output_update_usable_area(surface->view->output);
}

static void surface_handle_view_minimize(struct wl_listener *listener, void *data)
{
    struct kde_plasma_surface *surface = wl_container_of(listener, surface, view_minimize);
    kde_plasma_surface_set_usable_area(surface, !surface->view->base.minimized);
}

static void surface_handle_view_size(struct wl_listener *listener, void *data)
{
    struct kde_plasma_surface *surface = wl_container_of(listener, surface, view_size);
    kde_plasma_surface_set_usable_area(surface, true);
}

static void surface_handle_view_position(struct wl_listener *listener, void *data)
{
    struct kde_plasma_surface *surface = wl_container_of(listener, surface, view_position);
    kde_plasma_surface_set_usable_area(surface, true);
}

static void surface_handle_view_output(struct wl_listener *listener, void *data)
{
    struct kde_plasma_surface *surface = wl_container_of(listener, surface, view_output);
    struct kywc_output *old_output = data;

    if (!wl_list_empty(&surface->output_update_usable_area.link)) {
        wl_list_remove(&surface->output_update_usable_area.link);
        wl_list_init(&surface->output_update_usable_area.link);
        kywc_output_update_usable_area(old_output);
    }

    kde_plasma_surface_set_usable_area(surface, true);
}

static void surface_handle_view_map(struct wl_listener *listener, void *data)
{
    struct kde_plasma_surface *surface = wl_container_of(listener, surface, view_map);
    kde_plasma_surface_set_usable_area(surface, true);

    surface->view_minimize.notify = surface_handle_view_minimize;
    wl_signal_add(&surface->view->base.events.minimize, &surface->view_minimize);
    surface->view_size.notify = surface_handle_view_size;
    wl_signal_add(&surface->view->base.events.size, &surface->view_size);
    surface->view_position.notify = surface_handle_view_position;
    wl_signal_add(&surface->view->base.events.position, &surface->view_position);
    surface->view_output.notify = surface_handle_view_output;
    wl_signal_add(&surface->view->events.output, &surface->view_output);
}

static void surface_handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct kde_plasma_surface *surface = wl_container_of(listener, surface, view_unmap);

    wl_list_remove(&surface->view_minimize.link);
    wl_list_remove(&surface->view_size.link);
    wl_list_remove(&surface->view_position.link);
    wl_list_remove(&surface->view_output.link);

    kde_plasma_surface_set_usable_area(surface, false);
}

static void surface_handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct kde_plasma_surface *surface = wl_container_of(listener, surface, view_destroy);

    wl_list_remove(&surface->view_destroy.link);
    wl_list_remove(&surface->view_map.link);
    wl_list_remove(&surface->view_unmap.link);
    wl_list_remove(&surface->output_update_usable_area.link);

    surface->view = NULL;
}

static void surface_handle_map(struct wl_listener *listener, void *data)
{
    struct kde_plasma_surface *surface = wl_container_of(listener, surface, surface_map);

    /* useless once we connect surface and view */
    wl_list_remove(&surface->surface_map.link);
    wl_list_init(&surface->surface_map.link);

    /* get view from surface */
    surface->view = surface->wlr_surface->data;
    kde_plasma_surface_apply_role(surface);

    /* apply set_position called beform map */
    if (surface->x != INT32_MAX || surface->y != INT32_MAX) {
        surface->view->base.has_initial_position = true;
        kywc_view_move(&surface->view->base, surface->x, surface->y);
    }

    /* workaround to fix this listener is bebind view map */
    if (surface->view->base.mapped) {
        wl_list_init(&surface->view_map.link);
        surface_handle_view_map(&surface->view_map, NULL);
    } else {
        surface->view_map.notify = surface_handle_view_map;
        wl_signal_add(&surface->view->base.events.map, &surface->view_map);
    }
    surface->view_unmap.notify = surface_handle_view_unmap;
    wl_signal_add(&surface->view->base.events.unmap, &surface->view_unmap);
    surface->view_destroy.notify = surface_handle_view_destroy;
    wl_signal_add(&surface->view->base.events.destroy, &surface->view_destroy);
}

static void kde_plasma_surface_handle_resource_destroy(struct wl_resource *resource)
{
    struct kde_plasma_surface *surface = wl_resource_get_user_data(resource);
    wl_list_remove(&surface->surface_map.link);

    if (surface->view) {
        wl_list_remove(&surface->view_destroy.link);
        wl_list_remove(&surface->view_map.link);
        wl_list_remove(&surface->view_unmap.link);
        wl_list_remove(&surface->view_minimize.link);
        wl_list_remove(&surface->view_size.link);
        wl_list_remove(&surface->view_position.link);
        wl_list_remove(&surface->output_update_usable_area.link);
    }

    free(surface);
}

static void handle_get_surface(struct wl_client *client, struct wl_resource *shell_resource,
                               uint32_t id, struct wl_resource *surface_resource)
{
    struct wlr_surface *wlr_surface = wlr_surface_from_resource(surface_resource);
    if (!wlr_surface) {
        return;
    }

    /* create a plasma surface */
    struct kde_plasma_surface *surface = calloc(1, sizeof(struct kde_plasma_surface));
    if (!surface) {
        return;
    }

    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_plasma_surface_interface, PLASMA_SURFACE_VERSION, id);
    if (!resource) {
        free(surface);
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &kde_plasma_surface_impl, surface,
                                   kde_plasma_surface_handle_resource_destroy);

    surface->x = surface->y = INT32_MAX;
    surface->role = ORG_KDE_PLASMA_SURFACE_ROLE_NORMAL;
    wl_list_init(&surface->output_update_usable_area.link);

    surface->wlr_surface = wlr_surface;
    surface->surface_map.notify = surface_handle_map;
    wl_signal_add(&wlr_surface->events.map, &surface->surface_map);
}

static const struct org_kde_plasma_shell_interface kde_plasma_shell_impl = {
    .get_surface = handle_get_surface,
};

static void kde_plasma_shell_bind(struct wl_client *client, void *data, uint32_t version,
                                  uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_plasma_shell_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    struct kde_plasma_shell *shell = data;
    wl_resource_set_implementation(resource, &kde_plasma_shell_impl, shell, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct kde_plasma_shell *shell = wl_container_of(listener, shell, display_destroy);
    wl_list_remove(&shell->display_destroy.link);
    wl_global_destroy(shell->global);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct kde_plasma_shell *shell = wl_container_of(listener, shell, server_destroy);
    wl_list_remove(&shell->server_destroy.link);
    free(shell);
}

bool kde_plasma_shell_create(struct server *server)
{
    struct kde_plasma_shell *shell = calloc(1, sizeof(struct kde_plasma_shell));
    if (!shell) {
        return false;
    }

    shell->global = wl_global_create(server->display, &org_kde_plasma_shell_interface,
                                     PLASMA_SHELL_VERSION, shell, kde_plasma_shell_bind);
    if (!shell->global) {
        kywc_log(KYWC_WARN, "kde plasma shell create failed");
        free(shell);
        return false;
    }

    shell->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &shell->server_destroy);
    shell->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &shell->display_destroy);

    return true;
}
