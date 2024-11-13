// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>

#include <kywc/identifier.h>
#include <kywc/log.h>

#include "effect/fade.h"
#include "effect/scale.h"
#include "effect/slide.h"
#include "input/seat.h"
#include "output.h"
#include "scene/surface.h"
#include "server.h"
#include "theme.h"
#include "view/workspace.h"
#include "view_p.h"

static struct view_manager *view_manager = NULL;

struct view *view_manager_get_activated(void)
{
    return view_manager->activated.view;
}

void view_manager_add_activate_view_listener(struct wl_listener *listener)
{
    wl_signal_add(&view_manager->events.activate_view, listener);
}

struct view_layer *view_manager_get_layer(enum layer layer, bool in_workspace)
{
    if (!in_workspace) {
        return &view_manager->layers[layer];
    }

    switch (layer) {
    case LAYER_BELOW:
    case LAYER_NORMAL:
    case LAYER_ABOVE:
        return workspace_layer(workspace_manager_get_current(), layer);
    default:
        return &view_manager->layers[layer];
    }
}

static void view_update_output(struct view *view)
{
    int lx = 0, ly = 0;

    if (view->base.mapped) {
        struct kywc_box *geo = &view->base.geometry;
        /* update view most-at output */
        lx = geo->x + geo->width / 2;
        ly = geo->y + geo->height / 2;
    } else if (view->output) {
        /* no need to update output when unmapped except output was destroyed */
        return;
    }

    struct kywc_output *old = view->output;
    if (!old || old->destroying || !old->state.enabled ||
        !kywc_box_contains_point(&output_from_kywc_output(old)->geometry, lx, ly)) {
        view->output = kywc_output_at_point(lx, ly);
    }

    /* use fallback output if no valid output */
    if (!view->output) {
        view->output = output_manager_get_fallback();
    }

    if (old != view->output) {
        wl_list_remove(&view->output_destroy.link);
        wl_signal_add(&view->output->events.destroy, &view->output_destroy);
        wl_signal_emit_mutable(&view->events.output, old);
    }
}

static void view_handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct view *view = wl_container_of(listener, view, output_destroy);
    view->output = NULL;
    view_update_output(view);
}

static void view_fix_geometry_position(struct view *view, struct kywc_box *geo,
                                       struct kywc_box *src_box, struct kywc_box *dst_box)
{
    struct kywc_view *kywc_view = &view->base;
    /* actual view geometry with margin */
    int x = geo->x - kywc_view->margin.off_x;
    int y = geo->y - kywc_view->margin.off_y;
    int w = geo->width + kywc_view->margin.off_width;
    int h = geo->height + kywc_view->margin.off_height;

    /* keep align to edges */
    if (src_box->x == x) {
        geo->x = dst_box->x + kywc_view->margin.off_x;
    } else if (src_box->x + src_box->width == x + w) {
        geo->x = dst_box->x + dst_box->width - w + kywc_view->margin.off_x;
    } else {
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
        double frac_x = (double)dst_box->width / src_box->width;
        geo->x = ceil(MAX(x - src_box->x, 0) * frac_x) + dst_box->x + kywc_view->margin.off_x;
    }
    if (src_box->y == y) {
        geo->y = dst_box->y + kywc_view->margin.off_y;
    } else if (src_box->y + src_box->height == y + h) {
        geo->y = dst_box->y + dst_box->height - h + kywc_view->margin.off_y;
    } else {
        double frac_y = (double)dst_box->height / src_box->height;
        geo->y = ceil(MAX(y - src_box->y, 0) * frac_y) + dst_box->y + kywc_view->margin.off_y;
    }
#undef MAX
}

void view_move_to_output(struct view *view, struct kywc_box *src_box,
                         struct kywc_output *kywc_output)
{
    struct kywc_view *kywc_view = &view->base;
    struct output *dst = output_from_kywc_output(kywc_output);
    struct kywc_box *dst_box = &dst->usable_area;

    /* default to view output usable area */
    if (src_box == NULL) {
        src_box = &output_from_kywc_output(view->output)->usable_area;
    }

    if (kywc_view->fullscreen) {
        view_fix_geometry_position(view, &view->saved.geometry, src_box, dst_box);
        view_do_resize(view, &dst->geometry);
        return;
    }

    if (kywc_view->maximized) {
        struct kywc_box geo;
        view_get_tiled_geometry(view, &geo, kywc_output, KYWC_TILE_ALL);
        view_fix_geometry_position(view, &view->saved.geometry, src_box, dst_box);
        view_do_resize(view, &geo);
        return;
    }

    if (kywc_view->tiled) {
        struct kywc_box geo;
        view_get_tiled_geometry(view, &geo, kywc_output, kywc_view->tiled);
        view_fix_geometry_position(view, &view->saved.geometry, src_box, dst_box);
        view_do_resize(view, &geo);
        return;
    }

    if (!view_is_movable(view)) {
        return;
    }

    struct kywc_box geo = kywc_view->geometry;
    view_fix_geometry_position(view, &geo, src_box, dst_box);
    /* move to dst */
    view_do_move(view, geo.x, geo.y);
}

void view_init(struct view *view, const struct view_impl *impl, void *data)
{
    struct kywc_view *kywc_view = &view->base;

    wl_signal_init(&kywc_view->events.premap);
    wl_signal_init(&kywc_view->events.map);
    wl_signal_init(&kywc_view->events.unmap);
    wl_signal_init(&kywc_view->events.destroy);
    wl_signal_init(&kywc_view->events.activate);
    wl_signal_init(&kywc_view->events.maximize);
    wl_signal_init(&kywc_view->events.minimize);
    wl_signal_init(&kywc_view->events.fullscreen);
    wl_signal_init(&kywc_view->events.tile);
    wl_signal_init(&kywc_view->events.capabilities);
    wl_signal_init(&kywc_view->events.title);
    wl_signal_init(&kywc_view->events.app_id);
    wl_signal_init(&kywc_view->events.position);
    wl_signal_init(&kywc_view->events.size);
    wl_signal_init(&kywc_view->events.decoration);
    wl_signal_init(&kywc_view->events.unset_modal);

    view->impl = impl;
    view->data = data;
    wl_list_init(&view->children);
    wl_list_init(&view->parent_link);
    wl_list_init(&view->view_proxies);
    wl_signal_init(&view->events.parent);
    wl_signal_init(&view->events.workspace);
    wl_signal_init(&view->events.workspace_enter);
    wl_signal_init(&view->events.workspace_leave);
    wl_signal_init(&view->events.output);
    wl_signal_init(&view->events.icon_update);

    kywc_view->role = KYWC_VIEW_ROLE_NORMAL;
    kywc_view->minimizable = true;
    kywc_view->maximizable = true;
    kywc_view->fullscreenable = true;
    kywc_view->closeable = true;
    kywc_view->movable = true;
    kywc_view->resizable = true;
    kywc_view->activatable = true;
    kywc_view->focusable = true;
    kywc_view->has_round_corner = true;
    kywc_view->uuid = kywc_identifier_uuid_generate();
    kywc_view->focused_seat = input_manager_get_default_seat();
    wl_list_insert(&view_manager->views, &view->link);

    /* create view tree and disable it */
    struct view_layer *layer = view_manager_get_layer(LAYER_NORMAL, true);
    view->tree = ky_scene_tree_create(layer->tree);
    ky_scene_node_set_enabled(&view->tree->node, false);

    struct output *output = input_current_output(input_manager_get_default_seat());
    view->output = &output->base;
    view->output_destroy.notify = view_handle_output_destroy;
    wl_signal_add(&view->output->events.destroy, &view->output_destroy);

    view_set_workspace(view, workspace_manager_get_current());

    wl_signal_emit_mutable(&view_manager->events.new_view, kywc_view);
}

void view_get_tiled_geometry(struct view *view, struct kywc_box *geometry,
                             struct kywc_output *kywc_output, enum kywc_tile tile)
{
    struct output *output = output_from_kywc_output(kywc_output);
    struct kywc_box *usable = &output->usable_area;
    struct kywc_view *kywc_view = &view->base;
    int32_t min_width = kywc_view->min_width;
    int32_t min_height = kywc_view->min_height;
    int32_t half_width = usable->width / 2;
    int32_t half_height = usable->height / 2;
    bool use_min_width = half_width < min_width;
    bool use_min_height = half_height < min_height;

    switch (tile) {
    case KYWC_TILE_NONE:
        *geometry = view->saved.geometry;
        return;
    case KYWC_TILE_TOP:
        geometry->x = usable->x + kywc_view->margin.off_x;
        geometry->y = usable->y + kywc_view->margin.off_y;
        geometry->width = usable->width - kywc_view->margin.off_width;
        geometry->height = use_min_height ? min_height : half_height - kywc_view->margin.off_height;
        return;
    case KYWC_TILE_BOTTOM:
        geometry->x = usable->x + kywc_view->margin.off_x;
        geometry->y = use_min_height ? usable->y + usable->height - min_height -
                                           (kywc_view->margin.off_height - kywc_view->margin.off_y)
                                     : usable->y + half_height + kywc_view->margin.off_y;
        geometry->width = usable->width - kywc_view->margin.off_width;
        geometry->height = use_min_height ? min_height : half_height - kywc_view->margin.off_height;
        return;
    case KYWC_TILE_LEFT:
        geometry->x = usable->x + kywc_view->margin.off_x;
        geometry->y = usable->y + kywc_view->margin.off_y;
        geometry->width = use_min_width ? min_width : half_width - kywc_view->margin.off_width;
        geometry->height = usable->height - kywc_view->margin.off_height;
        return;
    case KYWC_TILE_RIGHT:
        geometry->x = use_min_width ? usable->x + usable->width - min_width -
                                          (kywc_view->margin.off_width - kywc_view->margin.off_x)
                                    : usable->x + half_width + kywc_view->margin.off_x;
        geometry->y = usable->y + kywc_view->margin.off_y;
        geometry->width = use_min_width ? min_width : half_width - kywc_view->margin.off_width;
        geometry->height = usable->height - kywc_view->margin.off_height;
        return;
    case KYWC_TILE_TOP_LEFT:
        geometry->x = usable->x + kywc_view->margin.off_x;
        geometry->y = usable->y + kywc_view->margin.off_y;
        geometry->width = use_min_width ? min_width : half_width - kywc_view->margin.off_width;
        geometry->height = use_min_height ? min_height : half_height - kywc_view->margin.off_height;
        return;
    case KYWC_TILE_BOTTOM_LEFT:
        geometry->x = usable->x + kywc_view->margin.off_x;
        geometry->y = use_min_height ? usable->y + usable->height - min_height -
                                           (kywc_view->margin.off_height - kywc_view->margin.off_y)
                                     : usable->y + half_height + kywc_view->margin.off_y;
        geometry->width = use_min_width ? min_width : half_width - kywc_view->margin.off_width;
        geometry->height = use_min_height ? min_height : half_height - kywc_view->margin.off_height;
        return;
    case KYWC_TILE_TOP_RIGHT:
        geometry->x = use_min_width ? usable->x + usable->width - min_width -
                                          (kywc_view->margin.off_width - kywc_view->margin.off_x)
                                    : usable->x + half_width + kywc_view->margin.off_x;
        geometry->y = usable->y + kywc_view->margin.off_y;
        geometry->width = use_min_width ? min_width : half_width - kywc_view->margin.off_width;
        geometry->height = use_min_height ? min_height : half_height - kywc_view->margin.off_height;
        return;
    case KYWC_TILE_BOTTOM_RIGHT:
        geometry->x = use_min_width ? usable->x + usable->width - min_width -
                                          (kywc_view->margin.off_width - kywc_view->margin.off_x)
                                    : usable->x + half_width + kywc_view->margin.off_x;
        geometry->y = use_min_height ? usable->y + usable->height - min_height -
                                           (kywc_view->margin.off_height - kywc_view->margin.off_y)
                                     : usable->y + half_height + kywc_view->margin.off_y;
        geometry->width = use_min_width ? min_width : half_width - kywc_view->margin.off_width;
        geometry->height = use_min_height ? min_height : half_height - kywc_view->margin.off_height;
        return;
    case KYWC_TILE_CENTER:
        geometry->x = use_min_width ? usable->x + half_width - min_width / 2 -
                                          (kywc_view->margin.off_width - kywc_view->margin.off_x)
                                    : usable->x + usable->width / 4 + kywc_view->margin.off_x;
        geometry->y = use_min_height ? usable->y + half_height - min_height / 2 -
                                           (kywc_view->margin.off_height - kywc_view->margin.off_y)
                                     : usable->y + usable->height / 4 + kywc_view->margin.off_y;
        geometry->width = use_min_width ? min_width : half_width - kywc_view->margin.off_width;
        geometry->height = use_min_height ? min_height : half_height - kywc_view->margin.off_height;
        return;
    case KYWC_TILE_ALL:
        geometry->x = usable->x + kywc_view->margin.off_x;
        geometry->y = usable->y + kywc_view->margin.off_y;
        geometry->width = usable->width - kywc_view->margin.off_width;
        geometry->height = usable->height - kywc_view->margin.off_height;
        return;
    }
}

struct wlr_buffer *view_get_icon_buffer(struct view *view, float scale)
{
    struct wlr_buffer *buf = theme_icon_get_buffer(view->icon, scale);
    if (!buf && view->impl->get_icon_buffer) {
        buf = view->impl->get_icon_buffer(view, scale);
    }
    return buf;
}

/* set surface round corner by ssd type, tiled, fullscreen and maximized state */
void view_update_round_corner(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    if (!kywc_view->mapped) {
        return;
    }

    struct ky_scene_buffer *buffer = ky_scene_buffer_try_from_surface(view->surface);
    if (!buffer) {
        return;
    }

    int radius[4] = { 0 };
    if (!kywc_view->has_round_corner) {
        ky_scene_node_set_radius(&buffer->node, radius);
        return;
    }

    /* don't draw top round corner if ssd has title */
    struct theme *theme = theme_manager_get_current();
    bool need_corner = !kywc_view->maximized && !kywc_view->fullscreen && !kywc_view->tiled;
    if (!view_manager->state.csd_round_corner) {
        need_corner &= kywc_view->ssd != KYWC_SSD_NONE;
    }
    bool need_top_corner = need_corner && !(kywc_view->ssd & KYWC_SSD_TITLE);

    radius[KY_SCENE_ROUND_CORNER_RB] = need_corner ? theme->corner_radius : 0;
    radius[KY_SCENE_ROUND_CORNER_RT] = need_top_corner ? theme->corner_radius : 0;
    radius[KY_SCENE_ROUND_CORNER_LB] = need_corner ? theme->corner_radius : 0;
    radius[KY_SCENE_ROUND_CORNER_LT] = need_top_corner ? theme->corner_radius : 0;
    ky_scene_node_set_radius(&buffer->node, radius);
}

void view_map(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;

    wl_signal_emit_mutable(&kywc_view->events.premap, NULL);

    view->icon = theme_icon_from_app_id(view->icon_name ? view->icon_name : kywc_view->app_id);
    if (view_manager->mode->impl->view_map) {
        view_manager->mode->impl->view_map(view);
    }
    /* assume that request_minimize may emitted before map */
    ky_scene_node_set_enabled(&view->tree->node, !kywc_view->minimized);

    kywc_view->mapped = true;

    if (view->pending.action) {
        /* add a fallback geometry */
        if (kywc_view->maximized || kywc_view->fullscreen) {
            view_get_tiled_geometry(view, &view->saved.geometry, view->output, KYWC_TILE_CENTER);
        }
        if (view->impl->configure) {
            view->impl->configure(view);
        }
    }

    kywc_view->has_initial_position = false;
    kywc_view_activate(kywc_view);
    seat_focus_surface(kywc_view->focused_seat, view->surface);

    view_update_round_corner(view);
    modal_create(view);

    kywc_log(KYWC_DEBUG, "kywc_view %p map", kywc_view);

    if (!view_add_slide_effect(view, true) && kywc_view->role == KYWC_VIEW_ROLE_NORMAL) {
        view_add_fade_effect(view, FADE_IN);
    }

    wl_signal_emit_mutable(&kywc_view->events.map, NULL);
    wl_signal_emit_mutable(&view_manager->events.new_mapped_view, kywc_view);

    struct view_proxy *proxy;
    wl_list_for_each(proxy, &view->view_proxies, view_link) {
        wl_signal_emit_mutable(&proxy->workspace->events.view_enter, view);
    }
    if (view->current_proxy && !kywc_view->minimized) {
        if (view_manager->show_desktop_enabled) {
            view_manager_show_desktop(false, false);
        }
        if (view_manager->show_activte_only_enabled) {
            view_manager_show_active_only(false, false);
        }
    }

    input_rebase_all_cursor();
}

void view_unmap(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    kywc_view->mapped = false;

    kywc_log(KYWC_DEBUG, "kywc_view %p unmap", kywc_view);

    if (!view_add_slide_effect(view, false) && kywc_view->role == KYWC_VIEW_ROLE_NORMAL &&
        !kywc_view->minimized) {
        view_add_fade_effect(view, FADE_OUT);
    }

    wl_signal_emit_mutable(&kywc_view->events.unmap, NULL);

    if (view_manager->mode->impl->view_unmap) {
        view_manager->mode->impl->view_unmap(view);
    }

    struct view_proxy *proxy;
    wl_list_for_each(proxy, &view->view_proxies, view_link) {
        wl_signal_emit_mutable(&proxy->workspace->events.view_leave, view);
    }

    kywc_view->title = kywc_view->app_id = NULL;
    ky_scene_node_set_enabled(&view->tree->node, false);

    if (view->pending.configure_timeout) {
        wl_event_source_remove(view->pending.configure_timeout);
        view->pending.configure_timeout = NULL;
    }

    wl_list_remove(&view->parent_link);
    wl_list_init(&view->parent_link);
    view->parent = NULL;

    struct view *child, *tmp;
    wl_list_for_each_safe(child, tmp, &view->children, parent_link) {
        wl_list_remove(&child->parent_link);
        wl_list_init(&child->parent_link);
        child->parent = NULL;
    }

    input_rebase_all_cursor();
    free(view->icon_name);
}

#define CONFIGURE_TIMEOUT_MS 100

static int view_handle_configure_timeout(void *data)
{
    struct view *view = data;

    kywc_log(KYWC_INFO, "client (%s) did not respond to configure request %d in %d ms",
             view->base.app_id, view->pending.configure_serial, CONFIGURE_TIMEOUT_MS);

    /* fallback for pending actions */
    if (view_action_change_size(view->pending.configure_action)) {
        struct kywc_box *pending = &view->pending.configure_geometry;
        int x = pending->x, y = pending->y;
        /* fix wobbling when user resize */
        if (view->pending.configure_action & VIEW_ACTION_RESIZE) {
            struct kywc_box *current = &view->base.geometry;
            uint32_t resize_edges = view->current_resize_edges;
            if (resize_edges & KYWC_EDGE_LEFT) {
                x += pending->width - current->width;
            }
            if (resize_edges & KYWC_EDGE_TOP) {
                y += pending->height - current->height;
            }
        }
        view_helper_move(view, x, y);
    }

    view_configured(view);

    return 0;
}

void view_configure(struct view *view, uint32_t serial)
{
    view->pending.configure_serial = serial;
    view->pending.configure_action |= view->pending.action;
    view->pending.configure_geometry = view->pending.geometry;

    view->pending.action = VIEW_ACTION_NOP;
    view->pending.geometry = (struct kywc_box){ 0 };
    view->pending.geometry = view->base.geometry;

    if (serial == 0) {
        return;
    }

    if (!view->pending.configure_timeout) {
        view->pending.configure_timeout = wl_event_loop_add_timer(
            view_manager->server->event_loop, view_handle_configure_timeout, view);
    }

    wl_event_source_timer_update(view->pending.configure_timeout, CONFIGURE_TIMEOUT_MS);
}

void view_configured(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;

    if (view->pending.configure_action & VIEW_ACTION_FULLSCREEN) {
        view_add_scale_effect(view, SCALE_MAXIMIZE);
        wl_signal_emit_mutable(&kywc_view->events.fullscreen, NULL);
    }

    if (view->pending.configure_action & VIEW_ACTION_MAXIMIZE) {
        view_add_scale_effect(view, SCALE_MAXIMIZE);
        wl_signal_emit_mutable(&kywc_view->events.maximize, NULL);
    }

    if (view->pending.configure_action & VIEW_ACTION_TILE) {
        wl_signal_emit_mutable(&kywc_view->events.tile, NULL);
    }

    if (view->pending.configure_action != VIEW_ACTION_NOP &&
        (view->pending.configure_action & VIEW_ACTION_RESIZE) == 0) {
        input_rebase_all_cursor();
    }

    if (view->pending.configure_action &
        (VIEW_ACTION_FULLSCREEN | VIEW_ACTION_TILE | VIEW_ACTION_MAXIMIZE)) {
        view_update_round_corner(view);
    }

    if (view->pending.configure_timeout) {
        wl_event_source_remove(view->pending.configure_timeout);
        view->pending.configure_timeout = NULL;
    }

    view->pending.configure_serial = 0;
    view->pending.configure_action = VIEW_ACTION_NOP;
    view->pending.configure_geometry = (struct kywc_box){ 0 };
    view->pending.geometry = (struct kywc_box){ 0 };
}

void view_proxy_destroy(struct view_proxy *view_proxy)
{
    if (!view_proxy) {
        return;
    }
    if (view_proxy->view->base.mapped) {
        wl_signal_emit_mutable(&view_proxy->workspace->events.view_leave, view_proxy->view);
        wl_signal_emit_mutable(&view_proxy->view->events.workspace_leave, view_proxy->workspace);
    }
    wl_list_remove(&view_proxy->view_link);
    wl_list_remove(&view_proxy->workspace_link);
    ky_scene_node_destroy(&view_proxy->tree->node);
    free(view_proxy);
}

void view_set_current_proxy(struct view *view, struct view_proxy *view_proxy)
{
    if (view->current_proxy == view_proxy) {
        return;
    }
    if (!view_proxy) {
        assert(wl_list_empty(&view->view_proxies));
    }
    if (view_proxy) {
        ky_scene_node_reparent(&view->tree->node, view_proxy->tree);
    }
    view->current_proxy = view_proxy;
    wl_signal_emit_mutable(&view->events.workspace, NULL);
}

static void view_proxies_destroy(struct view *view)
{
    struct view_proxy *proxy, *tmp;
    wl_list_for_each_safe(proxy, tmp, &view->view_proxies, view_link) {
        view_proxy_destroy(proxy);
    }
    view_set_current_proxy(view, NULL);
}

void view_destroy(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    kywc_log(KYWC_DEBUG, "kywc_view %p destroy", kywc_view);

    wl_signal_emit_mutable(&kywc_view->events.destroy, NULL);

    wl_list_remove(&view->link);
    wl_list_remove(&view->output_destroy.link);

    ky_scene_node_destroy(&view->tree->node);
    view_proxies_destroy(view);
    free((void *)kywc_view->uuid);

    view->impl->destroy(view);
}

void view_set_title(struct view *view, const char *title)
{
    struct kywc_view *kywc_view = &view->base;

    kywc_view->title = title;
    kywc_log(KYWC_DEBUG, "kywc_view %p title: %s", kywc_view, title);

    wl_signal_emit_mutable(&kywc_view->events.title, NULL);
}

void view_set_app_id(struct view *view, const char *app_id)
{
    struct kywc_view *kywc_view = &view->base;

    if (kywc_view->mapped && !view->icon_name) {
        view->icon = theme_icon_from_app_id(app_id);
        wl_signal_emit_mutable(&view->events.icon_update, NULL);
    }

    kywc_view->app_id = app_id;
    kywc_log(KYWC_DEBUG, "kywc_view %p app_id: %s", kywc_view, app_id);
    wl_signal_emit_mutable(&kywc_view->events.app_id, NULL);
}

void view_set_decoration(struct view *view, enum kywc_ssd ssd)
{
    struct kywc_view *kywc_view = &view->base;
    if (kywc_view->ssd == ssd) {
        return;
    }

    kywc_view->ssd = ssd;
    view_update_round_corner(view);
    kywc_log(KYWC_DEBUG, "kywc_view %p need ssd %d", kywc_view, ssd);

    wl_signal_emit_mutable(&kywc_view->events.decoration, NULL);
}

void view_set_icon_name(struct view *view, const char *icon_name)
{
    if ((!view->icon_name && !icon_name) ||
        (view->icon_name && icon_name && !strcmp(view->icon_name, icon_name))) {
        return;
    }

    free(view->icon_name);
    view->icon_name = icon_name ? strdup(icon_name) : NULL;
    view->icon = theme_icon_from_app_id(view->icon_name ? view->icon_name : view->base.app_id);

    wl_signal_emit_mutable(&view->events.icon_update, NULL);
}

struct view_proxy *view_proxy_by_workspace(struct view *view, struct workspace *workspace)
{
    struct view_proxy *view_proxy;
    wl_list_for_each(view_proxy, &view->view_proxies, view_link) {
        if (view_proxy->workspace == workspace) {
            return view_proxy;
        }
    }
    return NULL;
}

static struct view_proxy *view_proxy_create(struct view *view, struct workspace *workspace)
{
    struct view_proxy *proxy = calloc(1, sizeof(struct view_proxy));
    if (!proxy) {
        return NULL;
    }
    proxy->view = view;
    proxy->workspace = workspace;

    wl_list_insert(&workspace->view_proxies, &proxy->workspace_link);
    wl_list_insert(&view->view_proxies, &proxy->view_link);

    /* create view_proxy tree from new workspace view_layer */
    enum layer layer =
        view->base.kept_above ? LAYER_ABOVE : (view->base.kept_below ? LAYER_BELOW : LAYER_NORMAL);
    struct view_layer *view_layer = workspace_layer(workspace, layer);
    proxy->tree = ky_scene_tree_create(view_layer->tree);
    if (view->base.mapped) {
        wl_signal_emit_mutable(&workspace->events.view_enter, view);
        wl_signal_emit_mutable(&view->events.workspace_enter, workspace);
    }
    return proxy;
}

static void view_do_set_workspace(struct view *view, struct workspace *workspace)
{
    if (!workspace && !view->current_proxy) {
        return;
    }
    if (!workspace) {
        /* set workspace to null must reparent view tree first */
        view_proxies_destroy(view);
        wl_list_init(&view->view_proxies);
        return;
    }

    if (view->base.modal) {
        view_do_set_workspace(view->parent, workspace);
    }

    struct view_proxy *proxy = view_proxy_by_workspace(view, workspace);
    if (!proxy) {
        proxy = view_proxy_create(view, workspace);
        if (!proxy) {
            return;
        }
    }

    if (proxy->tree != view->tree->node.parent) {
        view_set_current_proxy(view, proxy);
    }

    // destroy all proxies except the new worskpace
    struct view_proxy *view_proxy, *tmp;
    wl_list_for_each_safe(view_proxy, tmp, &view->view_proxies, view_link) {
        if (view_proxy != proxy) {
            view_proxy_destroy(view_proxy);
        }
    }
    view->show_in_all_workspaces = false;
}

void view_set_workspace(struct view *view, struct workspace *workspace)
{
    assert(workspace);

    view_do_set_workspace(view, workspace);
}

void view_unset_workspace(struct view *view, struct view_layer *layer)
{
    assert(layer->layer != LAYER_BELOW && layer->layer != LAYER_NORMAL &&
           layer->layer != LAYER_ABOVE);

    ky_scene_node_reparent(&view->tree->node, layer->tree);
    view_do_set_workspace(view, NULL);
}

struct view_proxy *view_add_workspace(struct view *view, struct workspace *workspace)
{
    if (!view || !workspace) {
        return NULL;
    }

    if (view->base.modal) {
        view_add_workspace(view->parent, workspace);
    }

    struct view_proxy *view_proxy = view_proxy_by_workspace(view, workspace);
    if (view_proxy) {
        return NULL;
    }

    view_proxy = view_proxy_create(view, workspace);
    struct workspace *current_workspace = workspace_manager_get_current();
    if (workspace == current_workspace) {
        view_set_current_proxy(view, view_proxy);
    }
    return view_proxy;
}

void view_remove_workspace(struct view *view, struct workspace *workspace)
{
    if (!view || !workspace) {
        return;
    }

    if (view->base.modal) {
        view_remove_workspace(view->parent, workspace);
    }

    struct view_proxy *view_proxy = view_proxy_by_workspace(view, workspace);
    if (!view_proxy) {
        return;
    }

    struct view_proxy *proxy;
    wl_list_for_each_reverse(proxy, &view->view_proxies, view_link) {
        if (proxy != view_proxy) {
            break;
        }
    }
    /* add to all workspace if the view only exists in this workspace */
    if (&proxy->view_link == &view->view_proxies) {
        view_add_all_workspace(view);
        return;
    }
    /* del the proxy if view exists in multi workspaces */
    if (view->current_proxy == view_proxy) {
        view_set_current_proxy(view, proxy);
    }
    view_proxy_destroy(view_proxy);
    view->show_in_all_workspaces = false;
}

void view_add_all_workspace(struct view *view)
{
    if (!view) {
        return;
    }
    int workspace_num = workspace_manager_get_count();
    struct workspace *workspace = NULL;
    for (int i = 0; i < workspace_num; i++) {
        workspace = workspace_by_position(i);
        view_add_workspace(view, workspace);
    }
    view->show_in_all_workspaces = true;
}

void view_set_parent(struct view *view, struct view *parent)
{
    if (view->parent == parent) {
        return;
    }

    wl_list_remove(&view->parent_link);
    if (parent) {
        wl_list_insert(&parent->children, &view->parent_link);

        view->saved.layer = parent->saved.layer;
        struct ky_scene_tree *layer_tree = view_manager_get_layer_tree(&parent->tree->node);
        if (wl_list_empty(&view->view_proxies)) {
            ky_scene_node_reparent(&view->tree->node, layer_tree);
        } else {
            struct view_proxy *view_proxy;
            wl_list_for_each(view_proxy, &view->view_proxies, view_link) {
                ky_scene_node_reparent(&view_proxy->tree->node, layer_tree);
            }
        }
    } else {
        wl_list_init(&view->parent_link);
    }

    kywc_log(KYWC_DEBUG, "view %p set parent to %p", view, parent);

    view->parent = parent;

    wl_signal_emit_mutable(&view->events.parent, NULL);
}

void kywc_view_add_new_listener(struct wl_listener *listener)
{
    wl_signal_add(&view_manager->events.new_view, listener);
}

void kywc_view_add_new_mapped_listener(struct wl_listener *listener)
{
    wl_signal_add(&view_manager->events.new_mapped_view, listener);
}

struct kywc_view *kywc_view_by_uuid(const char *uuid)
{
    struct view *view;
    wl_list_for_each(view, &view_manager->views, link) {
        if (strcmp(uuid, view->base.uuid) == 0) {
            return &view->base;
        }
    }
    return NULL;
}

struct view *view_from_kywc_view(struct kywc_view *kywc_view)
{
    struct view *view = wl_container_of(kywc_view, view, base);
    return view;
}

struct view *view_try_from_wlr_surface(struct wlr_surface *wlr_surface)
{
    return wlr_surface->data;
}

void kywc_view_close(struct kywc_view *kywc_view)
{
    struct view *view = view_from_kywc_view(kywc_view);
    if (!view_is_closeable(view)) {
        return;
    }

    if (view->impl->close) {
        view->impl->close(view);
    }
}

void view_do_move(struct view *view, int x, int y)
{
    view->pending.action |= VIEW_ACTION_MOVE;
    view->pending.geometry.x = x;
    view->pending.geometry.y = y;

    if (view->base.mapped && view->impl->configure) {
        view->impl->configure(view);
    }
}

void kywc_view_move(struct kywc_view *kywc_view, int x, int y)
{
    if (view_manager->mode->impl->view_request_move) {
        view_manager->mode->impl->view_request_move(view_from_kywc_view(kywc_view), x, y);
    }
}

void view_do_resize(struct view *view, struct kywc_box *geometry)
{
    view->pending.action |= VIEW_ACTION_RESIZE;
    view->pending.geometry = *geometry;

    if (view->base.mapped && view->impl->configure) {
        view->impl->configure(view);
    }
}

void kywc_view_resize(struct kywc_view *kywc_view, struct kywc_box *geometry)
{
    if (view_manager->mode->impl->view_request_resize) {
        view_manager->mode->impl->view_request_resize(view_from_kywc_view(kywc_view), geometry);
    }
}

static void view_set_activated(struct view *view, bool activated);

static void handle_activated_view_minimized(struct wl_listener *listener, void *data)
{
    struct view *view = view_manager->activated.view;
    if (!view->base.minimized) {
        return;
    }

    view_set_activated(view, false);
    view_topmost_activate(workspace_manager_get_current());
}

static void handle_activated_view_unmap(struct wl_listener *listener, void *data)
{
    struct view *view = view_manager->activated.view;
    view_set_activated(view, false);
    view_topmost_activate(workspace_manager_get_current());
}

static void view_reparent_fullscreen(struct view *view, bool active);

static void view_set_activated(struct view *view, bool activated)
{
    struct kywc_view *kywc_view = &view->base;
    if (kywc_view->activated == activated) {
        return;
    }

    if (activated) {
        /* listen activated view's minimize and unmap signals,
         * so that we can auto activate another view.
         */
        view_manager->activated.view = view;
        wl_signal_add(&kywc_view->events.minimize, &view_manager->activated.minimize);
        wl_signal_add(&kywc_view->events.unmap, &view_manager->activated.unmap);
    } else {
        wl_list_remove(&view_manager->activated.minimize.link);
        wl_list_remove(&view_manager->activated.unmap.link);
        view_manager->activated.view = NULL;
    }

    /* change fullscreen layer when activation changed */
    if (kywc_view->fullscreen) {
        view_reparent_fullscreen(view, activated);
    }

    /* change parent fullscreen layer if parent is fullscreen */
    if (view->parent && view->parent->base.fullscreen) {
        view_reparent_fullscreen(view->parent, activated);
    }

    if (kywc_view->minimized && activated) {
        kywc_view_set_minimized(kywc_view, false);
    }

    kywc_view->activated = activated;
    view->pending.action |= VIEW_ACTION_ACTIVATE;

    if (kywc_view->mapped && view->impl->configure) {
        view->impl->configure(view);
    }

    wl_signal_emit_mutable(&kywc_view->events.activate, NULL);
    wl_signal_emit_mutable(&view_manager->events.activate_view, view_manager->activated.view);
}

void view_do_activate(struct view *view)
{
    if (view && !view_is_activatable(view)) {
        return;
    }

    struct view *last = view_manager->activated.view;
    if (last != view) {
        if (view_manager->show_activte_only_enabled) {
            view_manager_show_active_only(false, false);
        }
        if (last) {
            view_set_activated(last, false);
        }
        if (view) {
            view_set_activated(view, true);
        }
    }

    if (!view) {
        return;
    }

    struct workspace *workspace = view->current_proxy ? view->current_proxy->workspace : NULL;
    if (workspace && workspace != workspace_manager_get_current()) {
        workspace_activate_with_effect(view->current_proxy->workspace);
    }
}

void view_topmost_activate(struct workspace *workspace)
{
    if (!workspace) {
        workspace = workspace_manager_get_current();
    }

    struct view *view;
    struct view_proxy *view_proxy;
    /* find topmost enabled(mapped and not minimized) view and activate it */
    wl_list_for_each(view_proxy, &workspace->view_proxies, workspace_link) {
        view = view_proxy->view;
        if (!view->base.mapped || view->base.minimized || !view_is_activatable(view)) {
            continue;
        }
        view_do_activate(view);
        seat_focus_surface(input_manager_get_default_seat(), view->surface);
        if (view->base.fullscreen) {
            view_reparent_fullscreen(view, true);
        }
        return;
    }

    /* workaround to hide fullscreen view when no view in workspace */
    view = view_manager->activated.view;
    if (view && view->base.fullscreen) {
        view_proxy = view_proxy_by_workspace(view, workspace);
        if (!view_proxy) {
            view_reparent_fullscreen(view, false);
            view_set_activated(view, false);
        }
    }
}

void view_raise_to_top(struct view *view, bool find_parent)
{
    if (!view) {
        return;
    }

    if (view->parent && find_parent) {
        view_raise_to_top(view->parent, true);
    }

    if (view->current_proxy) {
        /* insert view proxy in workspace topmost */
        struct view_proxy *view_proxy;
        wl_list_for_each(view_proxy, &view->view_proxies, view_link) {
            wl_list_remove(&view_proxy->workspace_link);
            wl_list_insert(&view_proxy->workspace->view_proxies, &view_proxy->workspace_link);
            ky_scene_node_raise_to_top(&view_proxy->tree->node);
        }
    } else {
        ky_scene_node_raise_to_top(&view->tree->node);
    }

    /* raise children if any */
    struct view *child;
    wl_list_for_each(child, &view->children, parent_link) {
        view_raise_to_top(child, false);
    }
}

void kywc_view_activate(struct kywc_view *kywc_view)
{
    struct view *view = kywc_view ? view_from_kywc_view(kywc_view) : NULL;
    if (!view) {
        view_do_activate(NULL);
        return;
    }

    if (view_manager->mode->impl->view_request_activate) {
        view_manager->mode->impl->view_request_activate(view);
    }
}

void view_do_tiled(struct view *view, enum kywc_tile tile, struct kywc_output *kywc_output)
{
    struct kywc_view *kywc_view = &view->base;

    /* tiled mode may switch between outputs */
    if (kywc_view->tiled == tile && (!kywc_output || kywc_output == view->output)) {
        return;
    }

    struct kywc_box geo = { 0 };
    view_get_tiled_geometry(view, &geo, kywc_output ? kywc_output : view->output, tile);

    /* may switch between tiled modes */
    if (kywc_view->tiled == KYWC_TILE_NONE && tile != KYWC_TILE_NONE) {
        if (!kywc_view->maximized) {
            view->saved.geometry = view->base.geometry;
        }
    }

    if (kywc_view->maximized) {
        view->pending.action |= VIEW_ACTION_MAXIMIZE;
        kywc_view->maximized = false;
    }

    kywc_view->tiled = tile;
    view->pending.action |= VIEW_ACTION_TILE;
    view->pending.geometry = geo;

    if (kywc_view->mapped && view->impl->configure) {
        view->impl->configure(view);
    }
}

void kywc_view_set_tiled(struct kywc_view *kywc_view, enum kywc_tile tile,
                         struct kywc_output *kywc_output)
{
    if (view_manager->mode->impl->view_request_tiled) {
        view_manager->mode->impl->view_request_tiled(view_from_kywc_view(kywc_view), tile,
                                                     kywc_output);
    }
}

void view_do_minimized(struct view *view, bool minimized)
{
    struct kywc_view *kywc_view = &view->base;
    if (kywc_view->minimized == minimized) {
        return;
    }

    ky_scene_node_set_enabled(&view->tree->node, !minimized);

    kywc_view->minimized = minimized;
    view->pending.action |= VIEW_ACTION_MINIMIZE;

    if (kywc_view->mapped && view->impl->configure) {
        view->impl->configure(view);
    }

    /* if view is the activated view, process it in activated.minimize listener */
    view_add_scale_effect(view, SCALE_MINIMIZE);
    wl_signal_emit_mutable(&kywc_view->events.minimize, NULL);

    if (!kywc_view->minimized) {
        if (view_manager->show_desktop_enabled) {
            view_manager_show_desktop(false, false);
        }
        if (view_manager->show_activte_only_enabled) {
            view_manager_show_active_only(false, false);
        }
    }

    input_rebase_all_cursor();
}

void kywc_view_set_minimized(struct kywc_view *kywc_view, bool minimized)
{
    if (view_manager->mode->impl->view_request_minimized) {
        view_manager->mode->impl->view_request_minimized(view_from_kywc_view(kywc_view), minimized);
    }
}

void kywc_view_toggle_minimized(struct kywc_view *kywc_view)
{
    kywc_view_set_minimized(kywc_view, !kywc_view->minimized);
}

void view_do_maximized(struct view *view, bool maximized, struct kywc_output *kywc_output)
{
    struct kywc_view *kywc_view = &view->base;

    /* tiled to unmaximized after tiled from maximized */
    if (!kywc_view->tiled && kywc_view->maximized == maximized &&
        (!kywc_output || kywc_output == view->output)) {
        return;
    }

    struct kywc_box geo = { 0 };

    if (maximized) {
        view_get_tiled_geometry(view, &geo, kywc_output ? kywc_output : view->output,
                                KYWC_TILE_ALL);
        if (!kywc_view->tiled) {
            view->saved.geometry = kywc_view->geometry;
        }
    } else {
        geo = view->saved.geometry;
    }

    /* don't restore tiled mode followed other compositors */
    if (kywc_view->tiled) {
        view->pending.action |= VIEW_ACTION_TILE;
        kywc_view->tiled = KYWC_TILE_NONE;
    }

    kywc_view->maximized = maximized;
    view->pending.action |= VIEW_ACTION_MAXIMIZE;
    view->pending.geometry = geo;

    if (kywc_view->mapped && view->impl->configure) {
        view->impl->configure(view);
    }
}

void kywc_view_set_maximized(struct kywc_view *kywc_view, bool maximized,
                             struct kywc_output *kywc_output)
{
    if (view_manager->mode->impl->view_request_maximized) {
        view_manager->mode->impl->view_request_maximized(view_from_kywc_view(kywc_view), maximized,
                                                         kywc_output);
    }
}

void kywc_view_toggle_maximized(struct kywc_view *kywc_view)
{
    kywc_view_set_maximized(kywc_view, !kywc_view->maximized, NULL);
}

static void view_reparent_fullscreen(struct view *view, bool active)
{
    struct kywc_view *kywc_view = &view->base;
    struct view_proxy *view_proxy = NULL;
    struct view_layer *layer = NULL;
    if (active) {
        view->saved.layer = kywc_view->kept_above
                                ? LAYER_ABOVE
                                : (kywc_view->kept_below ? LAYER_BELOW : LAYER_NORMAL);
        layer = view_manager_get_layer(LAYER_ACTIVE, false);
    }

    wl_list_for_each(view_proxy, &view->view_proxies, view_link) {
        if (!active) {
            /* restore fullscreen view to workspace */
            layer = workspace_layer(view_proxy->workspace, view->saved.layer);
        }
        ky_scene_node_reparent(&view_proxy->tree->node, layer->tree);
    }

    struct view *child;
    wl_list_for_each(child, &view->children, parent_link) {
        view_reparent_fullscreen(child, active);
    }
}

void view_do_fullscreen(struct view *view, bool fullscreen, struct kywc_output *kywc_output)
{
    struct kywc_view *kywc_view = &view->base;

    if (kywc_view->fullscreen == fullscreen && (!kywc_output || kywc_output == view->output)) {
        return;
    }

    struct kywc_box geo = { 0 };

    if (fullscreen) {
        kywc_output_effective_geometry(kywc_output ? kywc_output : view->output, &geo);
        if (!kywc_view->maximized && !kywc_view->tiled) {
            view->saved.geometry = kywc_view->geometry;
        }
    } else if (kywc_view->maximized) {
        view_get_tiled_geometry(view, &geo, view->output, KYWC_TILE_ALL);
    } else if (kywc_view->tiled) {
        view_get_tiled_geometry(view, &geo, view->output, kywc_view->tiled);
    } else {
        geo = view->saved.geometry;
    }

    view_reparent_fullscreen(view, fullscreen);
    kywc_view->fullscreen = fullscreen;

    view->pending.action |= VIEW_ACTION_FULLSCREEN;
    view->pending.geometry = geo;

    if (kywc_view->mapped && view->impl->configure) {
        view->impl->configure(view);
    }
}

void kywc_view_set_fullscreen(struct kywc_view *kywc_view, bool fullscreen,
                              struct kywc_output *kywc_output)
{
    if (view_manager->mode->impl->view_request_fullscreen) {
        view_manager->mode->impl->view_request_fullscreen(view_from_kywc_view(kywc_view),
                                                          fullscreen, kywc_output);
    }
}

void kywc_view_toggle_fullscreen(struct kywc_view *kywc_view)
{
    kywc_view_set_fullscreen(kywc_view, !kywc_view->fullscreen, NULL);
}

void kywc_view_set_kept_above(struct kywc_view *kywc_view, bool kept_above)
{
    struct view *view = view_from_kywc_view(kywc_view);
    assert(view->current_proxy);

    if (kywc_view->kept_above == kept_above) {
        return;
    }

    kywc_view->kept_above = kept_above;
    kywc_view->kept_below = false;

    enum layer layer = kept_above ? LAYER_ABOVE : LAYER_NORMAL;
    struct view_proxy *proxy;
    wl_list_for_each(proxy, &view->view_proxies, view_link) {
        struct view_layer *view_layer = workspace_layer(proxy->workspace, layer);
        ky_scene_node_reparent(&proxy->tree->node, view_layer->tree);
    }
}

void kywc_view_toggle_kept_above(struct kywc_view *kywc_view)
{
    kywc_view_set_kept_above(kywc_view, !kywc_view->kept_above);
}

void kywc_view_set_kept_below(struct kywc_view *kywc_view, bool kept_below)
{
    struct view *view = view_from_kywc_view(kywc_view);
    assert(view->current_proxy);

    if (kywc_view->kept_below == kept_below) {
        return;
    }

    kywc_view->kept_below = kept_below;
    kywc_view->kept_above = false;

    enum layer layer = kept_below ? LAYER_BELOW : LAYER_NORMAL;
    struct view_proxy *proxy;
    wl_list_for_each(proxy, &view->view_proxies, view_link) {
        struct view_layer *view_layer = workspace_layer(proxy->workspace, layer);
        ky_scene_node_reparent(&proxy->tree->node, view_layer->tree);
    }
}

void kywc_view_toggle_kept_below(struct kywc_view *kywc_view)
{
    kywc_view_set_kept_below(kywc_view, !kywc_view->kept_below);
}

void view_helper_move(struct view *view, int x, int y)
{
    struct kywc_box *geo = &view->base.geometry;
    bool changed = geo->x != x || geo->y != y;

    geo->x = x;
    geo->y = y;

    /* if we have two monitor and place a window on the left one,
     * then unplug the left one, window will be placed to the right one by positioner.
     * the position will not change as the right one become (0,0).
     */
    view_update_output(view);

    if (changed) {
        ky_scene_node_set_position(&view->tree->node, x, y);
        wl_signal_emit_mutable(&view->base.events.position, NULL);
    }
}

static bool view_has_modal_child(struct view *view)
{
    struct view *child;
    wl_list_for_each(child, &view->children, parent_link) {
        if (child->base.mapped && (child->base.modal || view_has_modal_child(child))) {
            return true;
        }
    }
    return false;
}

bool view_has_modal_property(struct view *view)
{
    return view->base.modal || view_has_modal_child(view);
}

bool view_is_minimizable(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    if (!kywc_view->minimizable) {
        return false;
    }
    if (!view->minimized_when_show_desktop && view_has_modal_property(view)) {
        return false;
    }

    return true;
}

bool view_is_maximizable(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    if (!kywc_view->maximizable) {
        return false;
    }
    if (kywc_view->fullscreen) {
        return false;
    }
    if (view_has_modal_child(view)) {
        return false;
    }
    /* can not be maximize if view max size == min size */
    if (kywc_view->max_width != 0 && kywc_view->max_height != 0 &&
        kywc_view->min_width == kywc_view->max_width &&
        kywc_view->min_height == kywc_view->max_height) {
        return false;
    }

    return true;
}

bool view_is_fullscreenable(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    if (!kywc_view->fullscreenable) {
        return false;
    }
    if (view_has_modal_child(view)) {
        return false;
    }
    if (kywc_view->max_width != 0 && kywc_view->max_height != 0 &&
        kywc_view->min_width == kywc_view->max_width &&
        kywc_view->min_height == kywc_view->max_height) {
        return false;
    }

    return true;
}

bool view_is_closeable(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    if (!kywc_view->closeable) {
        return false;
    }
    /* it is not allowed to close a view that has children views */
    if (!wl_list_empty(&view->children)) {
        kywc_log(KYWC_WARN, "close a view that still have children views");
        return false;
    }

    return true;
}

bool view_is_movable(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    if (!kywc_view->movable) {
        return false;
    }
    if (kywc_view->fullscreen) {
        return false;
    }
    if (view_has_modal_child(view)) {
        return false;
    }

    return true;
}

bool view_is_resizable(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    if (!kywc_view->resizable) {
        return false;
    }
    /* can not be resize if view is fullscreen */
    if (kywc_view->fullscreen) {
        return false;
    }
    /* can not be resize if view has modal */
    if (view_has_modal_child(view)) {
        return false;
    }
    /* can not be resize if view max size == min size */
    if (kywc_view->max_width != 0 && kywc_view->max_height != 0 &&
        kywc_view->min_width == kywc_view->max_width &&
        kywc_view->min_height == kywc_view->max_height) {
        return false;
    }

    return true;
}

bool view_is_activatable(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    if (!kywc_view->activatable) {
        return false;
    }
    if (view_has_modal_child(view)) {
        return false;
    }

    return true;
}

bool view_is_focusable(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    if (!kywc_view->focusable) {
        return false;
    }
    if (view_has_modal_child(view)) {
        return false;
    }

    return true;
}

void view_apply_role(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    struct view_layer *layer = NULL;
    switch (kywc_view->role) {
    case KYWC_VIEW_ROLE_NORMAL:
        layer = view_manager_get_layer(LAYER_NORMAL, true);
        break;
    case KYWC_VIEW_ROLE_DESKTOP:
        layer = view_manager_get_layer(LAYER_DESKTOP, false);
        break;
    case KYWC_VIEW_ROLE_PANEL:
    case KYWC_VIEW_ROLE_APPLETPOPUP:
        layer = view_manager_get_layer(LAYER_DOCK, false);
        break;
    case KYWC_VIEW_ROLE_ONSCREENDISPLAY:
        layer = view_manager_get_layer(LAYER_ON_SCREEN_DISPLAY, false);
        break;
    case KYWC_VIEW_ROLE_NOTIFICATION:
        layer = view_manager_get_layer(LAYER_NOTIFICATION, false);
        break;
    case KYWC_VIEW_ROLE_TOOLTIP:
        layer = view_manager_get_layer(LAYER_POPUP, false);
        break;
    case KYWC_VIEW_ROLE_CRITICALNOTIFICATION:
        layer = view_manager_get_layer(LAYER_CRITICAL_NOTIFICATION, false);
        break;
    case KYWC_VIEW_ROLE_SYSTEMWINDOW:
        layer = view_manager_get_layer(LAYER_SYSTEM_WINDOW, false);
        break;
    case KYWC_VIEW_ROLE_SWITCHER:
        layer = view_manager_get_layer(LAYER_SWITCHER, false);
        break;
    case KYWC_VIEW_ROLE_INPUTPANEL:
        layer = view_manager_get_layer(LAYER_INPUT_PANEL, false);
        break;
    case KYWC_VIEW_ROLE_LOGOUT:
        layer = view_manager_get_layer(LAYER_LOGOUT, false);
        break;
    case KYWC_VIEW_ROLE_SCREENLOCKNOTIFICATION:
        layer = view_manager_get_layer(LAYER_SCREEN_LOCK_NOTIFICATION, false);
        break;
    case KYWC_VIEW_ROLE_WATERMARK:
        layer = view_manager_get_layer(LAYER_WATERMARK, false);
        break;
    case KYWC_VIEW_ROLE_SCREENLOCK:
        layer = view_manager_get_layer(LAYER_SCREEN_LOCK, false);
        break;
    case KYWC_VIEW_ROLE_AUTHENTICATION:
        layer = view_manager_get_layer(LAYER_CRITICAL_NOTIFICATION, false);
        global_authentication_create(view);
        break;
    }

    if (kywc_view->role == KYWC_VIEW_ROLE_NORMAL) {
        view_set_workspace(view_from_kywc_view(kywc_view), workspace_manager_get_current());
    } else {
        view_unset_workspace(view_from_kywc_view(kywc_view), layer);
    }

    kywc_view->minimizable = kywc_view->maximizable = kywc_view->fullscreenable =
        kywc_view->role == KYWC_VIEW_ROLE_NORMAL;
    kywc_view->closeable =
        kywc_view->role != KYWC_VIEW_ROLE_DESKTOP && kywc_view->role != KYWC_VIEW_ROLE_PANEL;
    kywc_view->movable = kywc_view->role == KYWC_VIEW_ROLE_NORMAL ||
                         kywc_view->role == KYWC_VIEW_ROLE_AUTHENTICATION;
    kywc_view->resizable =
        kywc_view->role == KYWC_VIEW_ROLE_NORMAL || kywc_view->role == KYWC_VIEW_ROLE_PANEL;
    kywc_view->activatable = kywc_view->role != KYWC_VIEW_ROLE_PANEL &&
                             kywc_view->role != KYWC_VIEW_ROLE_TOOLTIP &&
                             kywc_view->role != KYWC_VIEW_ROLE_WATERMARK &&
                             kywc_view->role != KYWC_VIEW_ROLE_NOTIFICATION &&
                             kywc_view->role != KYWC_VIEW_ROLE_SWITCHER;
    kywc_view->focusable = kywc_view->role == KYWC_VIEW_ROLE_NORMAL ||
                           kywc_view->role == KYWC_VIEW_ROLE_DESKTOP ||
                           kywc_view->role == KYWC_VIEW_ROLE_SYSTEMWINDOW ||
                           kywc_view->role == KYWC_VIEW_ROLE_SCREENLOCK ||
                           kywc_view->role == KYWC_VIEW_ROLE_APPLETPOPUP ||
                           kywc_view->role == KYWC_VIEW_ROLE_ONSCREENDISPLAY ||
                           kywc_view->role == KYWC_VIEW_ROLE_AUTHENTICATION;

    kywc_view->has_round_corner = kywc_view->role == KYWC_VIEW_ROLE_NORMAL ||
                                  kywc_view->role == KYWC_VIEW_ROLE_SYSTEMWINDOW ||
                                  kywc_view->role == KYWC_VIEW_ROLE_APPLETPOPUP ||
                                  kywc_view->role == KYWC_VIEW_ROLE_AUTHENTICATION;

    view_update_round_corner(view_from_kywc_view(kywc_view));
}

void view_update_size(struct view *view, int width, int height, int min_width, int min_height,
                      int max_width, int max_height)
{
    struct kywc_view *kywc_view = &view->base;

    if (kywc_view->min_width != min_width || kywc_view->min_height != min_height) {
        kywc_view->min_width = min_width;
        kywc_view->min_height = min_height;
        kywc_log(KYWC_DEBUG, "view %p minimal size to %d x %d", view, min_width, min_height);
    }

    if (kywc_view->max_width != max_width || kywc_view->max_height != max_height) {
        kywc_view->max_width = max_width;
        kywc_view->max_height = max_height;
        kywc_log(KYWC_DEBUG, "view %p maximal size to %d x %d", view, max_width, max_height);
    }

    if (kywc_view->geometry.width != width || kywc_view->geometry.height != height) {
        kywc_view->geometry.width = width;
        kywc_view->geometry.height = height;

        view_update_output(view);

        kywc_log(KYWC_DEBUG, "view %p size to %d x %d", view, width, height);
        wl_signal_emit_mutable(&view->base.events.size, NULL);
    }
}

void view_close_popups(struct view *view)
{
    if (view->impl->close_popups) {
        view->impl->close_popups(view);
    }
}

void view_show_window_menu(struct view *view, struct seat *seat, int x, int y)
{
    if (view_manager->mode->impl->view_request_show_menu) {
        view_manager->mode->impl->view_request_show_menu(view, seat, x, y);
    }
}

void view_manager_show_desktop(bool enabled, bool apply)
{
    if (view_manager->show_desktop_enabled == enabled) {
        return;
    }
    view_manager->show_desktop_enabled = enabled;

    /* minimize all view in current workspace */
    struct workspace *workspace = workspace_manager_get_current();
    struct view_proxy *view_proxy;
    wl_list_for_each_reverse(view_proxy, &workspace->view_proxies, workspace_link) {
        struct view *view = view_proxy->view;
        /* skip views not mapped */
        if (!view->base.mapped) {
            continue;
        }
        /* skip restoring views not minimized by show desktop */
        if (!enabled && !view->minimized_when_show_desktop) {
            continue;
        }

        /* true only the view is not minimized when going show desktop */
        if (enabled) {
            view->minimized_when_show_desktop = !view->base.minimized;
        }
        /* don't restoring views if the state is breaked */
        if (apply || view_has_modal_property(view)) {
            kywc_view_set_minimized(&view->base, enabled);
        }
        if (!enabled) {
            view->minimized_when_show_desktop = false;
        }
    }

    if (apply && !enabled) {
        view_topmost_activate(workspace);
    }

    wl_signal_emit_mutable(&view_manager->events.show_desktop, NULL);
}

void view_manager_add_show_desktop_listener(struct wl_listener *listener)
{
    wl_signal_add(&view_manager->events.show_desktop, listener);
}

bool view_manager_get_show_desktop(void)
{
    return view_manager->show_desktop_enabled;
}

bool view_manager_get_show_switcher(void)
{
    return view_manager->switcher_shown;
}

void view_manager_show_active_only(bool enabled, bool apply)
{
    if (view_manager->show_activte_only_enabled == enabled) {
        return;
    }
    view_manager->show_activte_only_enabled = enabled;

    struct view *current_view = view_manager_get_activated();
    if (!current_view) {
        return;
    }

    /* minimize all view in current workspace */
    struct workspace *workspace = workspace_manager_get_current();
    struct view_proxy *view_proxy;
    wl_list_for_each_reverse(view_proxy, &workspace->view_proxies, workspace_link) {
        struct view *view = view_proxy->view;
        if (!view->base.mapped || view == current_view || view_has_modal_property(view)) {
            continue;
        }

        /* skip restoring views not minimized by show only current view */
        if (!enabled && !view->minimized_when_show_active_only) {
            continue;
        }

        if (enabled) {
            view->minimized_when_show_active_only = !view->base.minimized;
        }
        /* don't restoring views if the state is breaked */
        if (apply) {
            if (view->base.minimized == enabled || !view_is_minimizable(view)) {
                continue;
            }

            ky_scene_node_set_enabled(&view->tree->node, !enabled);
            view->base.minimized = enabled;
            view->pending.action |= VIEW_ACTION_MINIMIZE;

            if (view->impl->configure) {
                view->impl->configure(view);
            }
            wl_signal_emit_mutable(&view->base.events.minimize, NULL);
        }

        if (!enabled) {
            view->minimized_when_show_active_only = false;
        }
    }
}

bool view_manager_get_show_activte_only(void)
{
    return view_manager->show_activte_only_enabled;
}

uint32_t view_manager_get_adsorption(void)
{
    return view_manager->state.view_adsorption;
}

struct ky_scene_tree *view_manager_get_layer_tree(struct ky_scene_node *node)
{
    struct ky_scene_tree *tree = node->parent;

    while (tree && tree->node.role != KY_SCENE_NODE_WORKSPACE &&
           tree->node.role != KY_SCENE_NODE_LAYER) {
        tree = tree->node.parent;
    }

    return tree;
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&view_manager->server_destroy.link);
    wl_list_remove(&view_manager->theme_update.link);
    wl_list_remove(&view_manager->theme_icon_update.link);

    struct view_mode *mode, *tmp;
    wl_list_for_each_safe(mode, tmp, &view_manager->view_modes, link) {
        if (mode->impl->mode_destroy) {
            mode->impl->mode_destroy();
        }
        view_manager_mode_unregister(mode);
    }

    for (int layer = LAYER_FIRST; layer < LAYER_NUMBER; layer++) {
        ky_scene_node_destroy(&view_manager->layers[layer].tree->node);
    }

    free(view_manager);
    view_manager = NULL;
}

static void handle_server_terminate(struct wl_listener *listener, void *data)
{
    view_manager->state.num_workspaces = workspace_manager_get_count();
    view_write_config(view_manager);
}

static void handle_theme_update(struct wl_listener *listener, void *data)
{
    struct theme_update_event *update_event = data;
    if (!(update_event->update_mask & THEME_UPDATE_MASK_CORNER_RADIUS)) {
        return;
    }
    struct view *view;
    wl_list_for_each(view, &view_manager->views, link) {
        view_update_round_corner(view);
    }
}

static void handle_theme_icon_update(struct wl_listener *listener, void *data)
{
    struct view *view;
    wl_list_for_each(view, &view_manager->views, link) {
        if (view->base.mapped) {
            view->icon =
                theme_icon_from_app_id(view->icon_name ? view->icon_name : view->base.app_id);
            wl_signal_emit_mutable(&view->events.icon_update, NULL);
        }
    }
}

void view_manager_set_switcher_shown(bool shown)
{
    view_manager->switcher_shown = shown;
}

void view_click(struct seat *seat, struct view *view, uint32_t button, bool pressed,
                enum click_state state)
{
    if (view_manager->mode->impl->view_click) {
        view_manager->mode->impl->view_click(seat, view, button, pressed, state);
    }
}

void view_hover(struct seat *seat, struct view *view)
{
    if (view_manager->mode->impl->view_hover) {
        view_manager->mode->impl->view_hover(seat, view);
    }
}

struct view_mode *view_manager_mode_from_name(const char *name)
{
    if (!name || !*name) {
        return NULL;
    }

    struct view_mode *mode;
    wl_list_for_each(mode, &view_manager->view_modes, link) {
        if (mode->impl->name && strcmp(mode->impl->name, name) == 0) {
            return mode;
        }
    }
    return NULL;
}

bool view_manager_set_view_mode(const char *name)
{
    struct view_mode *view_mode = view_manager_mode_from_name(name);
    if (!view_mode) {
        return false;
    }

    if (view_manager->mode == view_mode) {
        return true;
    }

    if (view_manager->mode && view_manager->mode->impl->view_mode_leave) {
        view_manager->mode->impl->view_mode_leave();
    }
    view_manager->mode = view_mode;
    if (view_manager->mode->impl->view_mode_enter) {
        view_manager->mode->impl->view_mode_enter();
    }
    return true;
}

bool view_has_descendant(struct view *view, struct view *descendant)
{
    if (!descendant) {
        return !wl_list_empty(&view->children);
    }

    struct view *tmp;
    wl_list_for_each(tmp, &view->children, parent_link) {
        if (tmp == descendant) {
            return true;
        }
        if (view_has_descendant(tmp, descendant)) {
            return true;
        }
    }

    return false;
}

bool view_has_ancestor(struct view *view, struct view *ancestor)
{
    if (!view->parent) {
        return false;
    }

    if (!ancestor) {
        return !!view->parent;
    }

    if (view->parent == ancestor) {
        return true;
    }

    return view_has_ancestor(view->parent, ancestor);
}

void view_move_to_center(struct view *view)
{
    struct output *output = output_from_kywc_output(view->output);
    int x_center = output->geometry.x + output->geometry.width / 2;
    int y_center = output->geometry.y + output->geometry.height / 2;
    int x = x_center - view->base.geometry.width / 2;
    int y = y_center - view->base.geometry.height / 2;

    view_do_move(view, x, y);
}

struct view_mode *view_manager_mode_register(const struct view_mode_interface *impl)
{
    struct view_mode *mode = malloc(sizeof(struct view_mode));
    if (!mode) {
        return NULL;
    }

    wl_list_insert(&view_manager->view_modes, &mode->link);
    mode->impl = impl;
    return mode;
}

void view_manager_mode_unregister(struct view_mode *mode)
{
    wl_list_remove(&mode->link);
    free(mode);
}

static void view_manager_modes_register(struct view_manager *view_manager)
{
    wl_list_init(&view_manager->view_modes);
    stack_mode_register(view_manager);
}

struct view_manager *view_manager_create(struct server *server)
{
    view_manager = calloc(1, sizeof(struct view_manager));
    if (!view_manager) {
        return NULL;
    }

    view_manager->server = server;
    wl_list_init(&view_manager->views);
    wl_signal_init(&view_manager->events.new_view);
    wl_signal_init(&view_manager->events.new_mapped_view);
    wl_signal_init(&view_manager->events.show_desktop);
    wl_signal_init(&view_manager->events.activate_view);

    view_manager->theme_update.notify = handle_theme_update;
    theme_manager_add_update_listener(&view_manager->theme_update);
    view_manager->theme_icon_update.notify = handle_theme_icon_update;
    theme_manager_add_icon_update_listener(&view_manager->theme_icon_update);
    view_manager->server_terminate.notify = handle_server_terminate;
    wl_signal_add(&server->events.terminate, &view_manager->server_terminate);
    view_manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &view_manager->server_destroy);

    view_manager->activated.minimize.notify = handle_activated_view_minimized;
    view_manager->activated.unmap.notify = handle_activated_view_unmap;

    /* create all layers */
    for (int layer = LAYER_FIRST; layer < LAYER_NUMBER; layer++) {
        view_manager->layers[layer].layer = layer;
        view_manager->layers[layer].tree = ky_scene_tree_create(&server->scene->tree);
        view_manager->layers[layer].tree->node.role = KY_SCENE_NODE_LAYER;
    }

    view_manager_modes_register(view_manager);
    view_manager_config_init(view_manager);

    view_manager->state.num_workspaces = 4;
    view_manager->state.view_adsorption = VIEW_ADSORPTION_ALL;
    view_read_config(view_manager);

    if (!view_manager->mode) {
        view_manager->mode = view_manager_mode_from_name("stack_mode");
    }
    if (view_manager->mode->impl->view_mode_enter) {
        view_manager->mode->impl->view_mode_enter();
    }
    workspace_manager_create(view_manager);
    decoration_manager_create(view_manager);
    server_decoration_manager_create(view_manager);
    positioner_manager_create(view_manager);
    window_actions_create(view_manager);
    window_menu_manager_create(view_manager);
    view_manager_actions_create(view_manager);
    maximize_switcher_create(view_manager);

    xdg_shell_init(view_manager);

    wlr_layer_shell_manager_create(server);
    wlr_foreign_toplevel_manager_create(server);
    ky_toplevel_manager_create(server);
    kde_plasma_shell_create(server);
    kde_plasma_window_management_create(server);
    kde_blur_manager_create(server);
    kde_slide_manager_create(server);
    xdg_dialog_create(server);
    xdg_activation_create(server);
    ukui_shell_create(server);
    ukui_window_management_create(server);
    ukui_blur_manager_create(server);

    return view_manager;
}
