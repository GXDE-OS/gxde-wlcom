// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_compositor.h>

#include "output.h"
#include "plasma-shell-protocol.h"
#include "view/workspace.h"
#include "view_p.h"

#define PLASMA_SURFACE_VERSION 7
#define PLASMA_SHELL_VERSION 7

struct kde_plasma_shell {
    struct wl_global *global;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct kde_plasma_surface {
    struct kde_plasma_shell *shell;

    struct wlr_surface *wlr_surface;
    struct wl_listener surface_map;
    struct wl_listener surface_destroy;

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
    bool skip_taskbar, skip_switcher;
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
    if (!surface->wlr_surface) {
        return;
    }

    surface->x = x;
    surface->y = y;

    if (surface->view) {
        kywc_view_move(&surface->view->base, surface->x, surface->y);
    }
}

static void kde_plasma_surface_apply_role(struct kde_plasma_surface *surface)
{
    struct view_layer *layer = NULL;
    struct kywc_view *kywc_view = &surface->view->base;

    switch (surface->role) {
    case ORG_KDE_PLASMA_SURFACE_ROLE_NORMAL:
        layer = view_manager_get_layer(LAYER_NORMAL, true);
        kywc_view->role = KYWC_VIEW_ROLE_NORMAL;
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_DESKTOP:
        layer = view_manager_get_layer(LAYER_DESKTOP, false);
        kywc_view->role = KYWC_VIEW_ROLE_DESKTOP;
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_PANEL:
        layer = view_manager_get_layer(LAYER_DOCK, false);
        kywc_view->role = KYWC_VIEW_ROLE_PANEL;
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_TOOLTIP:
        layer = view_manager_get_layer(LAYER_POPUP, false);
        kywc_view->role = KYWC_VIEW_ROLE_TOOLTIP;
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_ONSCREENDISPLAY:
        layer = view_manager_get_layer(LAYER_ON_SCREEN_DISPLAY, false);
        kywc_view->role = KYWC_VIEW_ROLE_ONSCREENDISPLAY;
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_NOTIFICATION:
        layer = view_manager_get_layer(LAYER_NOTIFICATION, false);
        kywc_view->role = KYWC_VIEW_ROLE_NOTIFICATION;
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_CRITICALNOTIFICATION:
        layer = view_manager_get_layer(LAYER_CRITICAL_NOTIFICATION, false);
        kywc_view->role = KYWC_VIEW_ROLE_CRITICALNOTIFICATION;
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_SYSTEMWINDOW:
        layer = view_manager_get_layer(LAYER_SYSTEM_WINDOW, false);
        kywc_view->role = KYWC_VIEW_ROLE_SYSTEMWINDOW;
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_INPUTPANEL:
        layer = view_manager_get_layer(LAYER_INPUT_PANEL, false);
        kywc_view->role = KYWC_VIEW_ROLE_INPUTPANEL;
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_LOGOUT:
        layer = view_manager_get_layer(LAYER_LOGOUT, false);
        kywc_view->role = KYWC_VIEW_ROLE_LOGOUT;
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_SCREENLOCK:
        layer = view_manager_get_layer(LAYER_SCREEN_LOCK, false);
        kywc_view->role = KYWC_VIEW_ROLE_SCREENLOCK;
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_SCREENLOCKNOTIFICATION:
        layer = view_manager_get_layer(LAYER_SCREEN_LOCK_NOTIFICATION, false);
        kywc_view->role = KYWC_VIEW_ROLE_SCREENLOCKNOTIFICATION;
        break;
    case ORG_KDE_PLASMA_SURFACE_ROLE_WATERMARK:
        layer = view_manager_get_layer(LAYER_WATERMARK, false);
        kywc_view->role = KYWC_VIEW_ROLE_WATERMARK;
        break;
    }

    if (surface->role == ORG_KDE_PLASMA_SURFACE_ROLE_NORMAL) {
        view_set_workspace(surface->view, workspace_manager_get_current());
    } else {
        view_unset_workspace(surface->view, layer);
    }

    kywc_view->minimizable = kywc_view->maximizable = kywc_view->fullscreenable =
        surface->role == ORG_KDE_PLASMA_SURFACE_ROLE_NORMAL;
    kywc_view->closeable = surface->role != ORG_KDE_PLASMA_SURFACE_ROLE_DESKTOP &&
                           surface->role != ORG_KDE_PLASMA_SURFACE_ROLE_PANEL;
    kywc_view->movable = kywc_view->resizable = surface->role == ORG_KDE_PLASMA_SURFACE_ROLE_NORMAL;
    kywc_view->activatable = surface->role != ORG_KDE_PLASMA_SURFACE_ROLE_PANEL &&
                             surface->role != ORG_KDE_PLASMA_SURFACE_ROLE_TOOLTIP &&
                             surface->role != ORG_KDE_PLASMA_SURFACE_ROLE_WATERMARK;
    kywc_view->focusable = surface->role == ORG_KDE_PLASMA_SURFACE_ROLE_NORMAL ||
                           surface->role == ORG_KDE_PLASMA_SURFACE_ROLE_DESKTOP ||
                           surface->role == ORG_KDE_PLASMA_SURFACE_ROLE_SYSTEMWINDOW ||
                           surface->role == ORG_KDE_PLASMA_SURFACE_ROLE_SCREENLOCK;
    kywc_view->shadeable = surface->role == ORG_KDE_PLASMA_SURFACE_ROLE_NORMAL;

    kywc_view->has_round_corner = surface->role == ORG_KDE_PLASMA_SURFACE_ROLE_NORMAL;
}

static void kde_plasma_surface_set_usable_area(struct kde_plasma_surface *surface, bool enabled);
static void handle_set_role(struct wl_client *client, struct wl_resource *resource, uint32_t role)
{
    struct kde_plasma_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface) {
        return;
    }

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
    struct kde_plasma_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface) {
        return;
    }

    surface->skip_taskbar = skip;

    if (surface->view && surface->skip_taskbar != surface->view->base.skip_taskbar) {
        surface->view->base.skip_taskbar = surface->skip_taskbar;
        wl_signal_emit_mutable(&surface->view->base.events.capabilities, NULL);
    }
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
    struct kde_plasma_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface || surface->role != ORG_KDE_PLASMA_SURFACE_ROLE_PANEL) {
        return;
    }

    if (surface->view && surface->view->base.focusable != takes_focus) {
        surface->view->base.focusable = surface->view->base.activatable = takes_focus;
    }
}

static void handle_set_skip_switcher(struct wl_client *client, struct wl_resource *resource,
                                     uint32_t skip)
{
    struct kde_plasma_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface) {
        return;
    }

    surface->skip_switcher = skip;

    if (surface->view && surface->skip_switcher != surface->view->base.skip_switcher) {
        surface->view->base.skip_switcher = surface->skip_switcher;
        wl_signal_emit_mutable(&surface->view->base.events.capabilities, NULL);
    }
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

    surface->view = NULL;
}

static void surface_handle_map(struct wl_listener *listener, void *data)
{
    struct kde_plasma_surface *surface = wl_container_of(listener, surface, surface_map);

    /* useless once we connect surface and view */
    wl_list_remove(&surface->surface_map.link);
    wl_list_init(&surface->surface_map.link);

    /* get view from surface */
    surface->view = view_try_from_wlr_surface(surface->wlr_surface);
    if (!surface->view) {
        kywc_log(KYWC_WARN, "surface is not a toplevel");
        return;
    }

    surface->view->base.skip_taskbar = surface->skip_taskbar;
    surface->view->base.skip_switcher = surface->skip_switcher;
    kde_plasma_surface_apply_role(surface);

    /* apply set_position called beform map */
    if (surface->x != INT32_MAX || surface->y != INT32_MAX) {
        surface->view->base.has_initial_position = true;
        kywc_view_move(&surface->view->base, surface->x, surface->y);
    }

    surface->view_map.notify = surface_handle_view_map;
    wl_signal_add(&surface->view->base.events.map, &surface->view_map);
    surface->view_unmap.notify = surface_handle_view_unmap;
    wl_signal_add(&surface->view->base.events.unmap, &surface->view_unmap);
    surface->view_destroy.notify = surface_handle_view_destroy;
    wl_signal_add(&surface->view->base.events.destroy, &surface->view_destroy);

    /* workaround to fix this listener is behind view map */
    if (surface->view->base.mapped) {
        surface_handle_view_map(&surface->view_map, NULL);
    }
}

static void surface_handle_destroy(struct wl_listener *listener, void *data)
{
    struct kde_plasma_surface *surface = wl_container_of(listener, surface, surface_destroy);

    wl_list_remove(&surface->surface_map.link);
    wl_list_remove(&surface->surface_destroy.link);

    surface->wlr_surface = NULL;
}

static void kde_plasma_surface_handle_resource_destroy(struct wl_resource *resource)
{
    struct kde_plasma_surface *surface = wl_resource_get_user_data(resource);

    if (surface->wlr_surface) {
        surface_handle_destroy(&surface->surface_destroy, NULL);
    }

    if (surface->view) {
        if (surface->view->base.mapped) {
            surface_handle_view_unmap(&surface->view_unmap, NULL);
        }
        surface_handle_view_destroy(&surface->view_destroy, NULL);
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
    surface->surface_destroy.notify = surface_handle_destroy;
    wl_signal_add(&wlr_surface->events.destroy, &surface->surface_destroy);

    /* workaround to fix this request is behind surface map */
    if (surface->wlr_surface->mapped) {
        surface_handle_map(&surface->surface_map, NULL);
    }
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
