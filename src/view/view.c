#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>

#include <kywc/log.h>

#include "input/seat.h"
#include "output.h"
#include "server.h"
#include "view/workspace.h"
#include "view_p.h"

static struct view_manager *view_manager = NULL;

struct view *view_manager_get_activated(void)
{
    return view_manager->activated.view;
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
        /* udpate view most-at output */
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

void view_init(struct view *view, const struct view_impl *impl, void *data)
{
    struct kywc_view *kywc_view = &view->base;
    wlr_addon_set_init(&view->addons);

    wl_signal_init(&kywc_view->events.premap);
    wl_signal_init(&kywc_view->events.map);
    wl_signal_init(&kywc_view->events.unmap);
    wl_signal_init(&kywc_view->events.destroy);
    wl_signal_init(&kywc_view->events.activate);
    wl_signal_init(&kywc_view->events.maximize);
    wl_signal_init(&kywc_view->events.minimize);
    wl_signal_init(&kywc_view->events.fullscreen);
    wl_signal_init(&kywc_view->events.tile);
    wl_signal_init(&kywc_view->events.title);
    wl_signal_init(&kywc_view->events.app_id);
    wl_signal_init(&kywc_view->events.position);
    wl_signal_init(&kywc_view->events.size);
    wl_signal_init(&kywc_view->events.decoration);
    wl_signal_init(&kywc_view->events.shadow);

    view->impl = impl;
    view->data = data;
    wl_list_init(&view->link);
    wl_list_init(&view->children);
    wl_list_init(&view->parent_link);
    wl_signal_init(&view->events.parent);
    wl_signal_init(&view->events.workspace);
    wl_signal_init(&view->events.output);

    kywc_view->minimizable = true;
    kywc_view->maximizable = true;
    kywc_view->fullscreenable = true;
    kywc_view->closeable = true;
    kywc_view->movable = true;
    kywc_view->resizable = true;
    kywc_view->activatable = true;
    kywc_view->focusable = true;

    /* create view tree and disable it */
    struct view_layer *layer = view_manager_get_layer(LAYER_NORMAL, true);
    view->tree = ky_scene_tree_create(layer->tree);
    ky_scene_node_set_enabled(ky_scene_node_from_tree(view->tree), false);
    view->content = ky_scene_tree_create(view->tree);

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

    switch (tile) {
    case KYWC_TILE_NONE:
        *geometry = view->saved.geometry;
        return;
    case KYWC_TILE_UP:
        geometry->x = usable->x + kywc_view->margin.off_x;
        geometry->y = usable->y + kywc_view->margin.off_y;
        geometry->width = usable->width - kywc_view->margin.off_width;
        geometry->height = usable->height / 2 - kywc_view->margin.off_height;
        return;
    case KYWC_TILE_DOWN:
        geometry->x = usable->x + kywc_view->margin.off_x;
        geometry->y = usable->y + usable->height / 2 + kywc_view->margin.off_y;
        geometry->width = usable->width - kywc_view->margin.off_width;
        geometry->height = usable->height / 2 - kywc_view->margin.off_height;
        return;
    case KYWC_TILE_LEFT:
        geometry->x = usable->x + kywc_view->margin.off_x;
        geometry->y = usable->y + kywc_view->margin.off_y;
        geometry->width = usable->width / 2 - kywc_view->margin.off_width;
        geometry->height = usable->height - kywc_view->margin.off_height;
        return;
    case KYWC_TILE_RIGHT:
        geometry->x = usable->x + usable->width / 2 + kywc_view->margin.off_x;
        geometry->y = usable->y + kywc_view->margin.off_y;
        geometry->width = usable->width / 2 - kywc_view->margin.off_width;
        geometry->height = usable->height - kywc_view->margin.off_height;
        return;
    case KYWC_TILE_CENTER:
        geometry->x = usable->x + usable->width / 4 + kywc_view->margin.off_x;
        geometry->y = usable->y + usable->height / 4 + kywc_view->margin.off_y;
        geometry->width = usable->width / 2 - kywc_view->margin.off_width;
        geometry->height = usable->height / 2 - kywc_view->margin.off_height;
        return;
    case KYWC_TILE_ALL:
        geometry->x = usable->x + kywc_view->margin.off_x;
        geometry->y = usable->y + kywc_view->margin.off_y;
        geometry->width = usable->width - kywc_view->margin.off_width;
        geometry->height = usable->height - kywc_view->margin.off_height;
        return;
    }
}

void view_map(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;

    wl_signal_emit_mutable(&kywc_view->events.premap, NULL);

    /* assume that request_minimize may emited before map */
    struct ky_scene_node *node = ky_scene_node_from_tree(view->tree);
    ky_scene_node_set_enabled(node, !kywc_view->minimized);

    kywc_view_activate(kywc_view);

    if (kywc_view->focusable) {
        seat_focus_surface(input_manager_get_default_seat(), view->surface);
    }

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
    kywc_view->mapped = true;

    kywc_log(KYWC_DEBUG, "kywc_view %p map", kywc_view);
    wl_signal_emit_mutable(&kywc_view->events.map, NULL);
}

void view_unmap(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;

    kywc_view->title = kywc_view->app_id = NULL;
    ky_scene_node_set_enabled(ky_scene_node_from_tree(view->tree), false);
    kywc_view->mapped = false;

    if (view->pending.configure_timeout) {
        wl_event_source_remove(view->pending.configure_timeout);
        view->pending.configure_timeout = NULL;
    }

    kywc_log(KYWC_DEBUG, "kywc_view %p unmap", kywc_view);
    wl_signal_emit_mutable(&kywc_view->events.unmap, NULL);
}

#define CONFIGURE_TIMEOUT_MS 100

static int view_handle_configure_timeout(void *data)
{
    struct view *view = data;

    kywc_log(KYWC_INFO, "client (%s) did not respond to configure request in %d ms",
             view->base.app_id, CONFIGURE_TIMEOUT_MS);

    /* fallback for pending actions */
    if (view_action_change_size(view->pending.action)) {
        struct kywc_box *current = &view->base.geometry;
        struct kywc_box *pending = &view->pending.geometry;
        int x = pending->x, y = pending->y;
        /* fix wobbling when resize */
        if (view->pending.action & VIEW_ACTION_RESIZE) {
            if (current->x != pending->x) {
                x += pending->width - current->width;
            }
            if (current->y != pending->y) {
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

    if (!view->pending.configure_timeout) {
        view->pending.configure_timeout = wl_event_loop_add_timer(
            view_manager->server->event_loop, view_handle_configure_timeout, view);
    }

    wl_event_source_timer_update(view->pending.configure_timeout, CONFIGURE_TIMEOUT_MS);
}

void view_configured(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;

    if (view->pending.action & VIEW_ACTION_FULLSCREEN) {
        wl_signal_emit_mutable(&kywc_view->events.fullscreen, NULL);
    }

    if (view->pending.action & VIEW_ACTION_MAXIMIZE) {
        wl_signal_emit_mutable(&kywc_view->events.maximize, NULL);
    }

    if (view->pending.action & VIEW_ACTION_RESIZE) {
        kywc_view->resizing = false;
    }

    if (view->pending.action & VIEW_ACTION_TILE) {
        wl_signal_emit_mutable(&kywc_view->events.tile, NULL);
    }

    if (view->pending.configure_timeout) {
        wl_event_source_remove(view->pending.configure_timeout);
        view->pending.configure_timeout = NULL;
    }

    view->pending.configure_serial = 0;
    view->pending.action = VIEW_ACTION_NOP;
}

void view_destroy(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    kywc_log(KYWC_DEBUG, "kywc_view %p destroy", kywc_view);

    wlr_addon_set_finish(&view->addons);
    wl_signal_emit_mutable(&kywc_view->events.destroy, NULL);

    wl_list_remove(&view->link);
    wl_list_remove(&view->parent_link);
    wl_list_remove(&view->output_destroy.link);

    /* there should be no children views when destroy */
    struct view *child, *tmp;
    wl_list_for_each_safe(child, tmp, &view->children, parent_link) {
        wl_list_remove(&child->parent_link);
        wl_list_init(&child->parent_link);
        child->parent = NULL;
    }

    ky_scene_node_destroy(ky_scene_node_from_tree(view->tree));
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

    kywc_view->app_id = app_id;
    kywc_log(KYWC_DEBUG, "kywc_view %p app_id: %s", kywc_view, app_id);

    wl_signal_emit_mutable(&kywc_view->events.app_id, NULL);
}

void view_set_decoration(struct view *view, bool need_ssd)
{
    struct kywc_view *kywc_view = &view->base;
    if (kywc_view->need_ssd == need_ssd) {
        return;
    }

    kywc_view->need_ssd = need_ssd;
    kywc_log(KYWC_DEBUG, "kywc_view %p need ssd %d", kywc_view, need_ssd);

    wl_signal_emit_mutable(&kywc_view->events.decoration, NULL);
}

void view_set_shadow(struct view *view, bool need_shadow)
{
    struct kywc_view *kywc_view = &view->base;
    if (kywc_view->need_shadow == need_shadow) {
        return;
    }

    kywc_view->need_shadow = need_shadow;
    kywc_log(KYWC_DEBUG, "kywc_view %p need shadow %d", kywc_view, need_shadow);

    wl_signal_emit_mutable(&kywc_view->events.shadow, NULL);
}

void view_set_workspace(struct view *view, struct workspace *workspace)
{
    if (view->workspace == workspace) {
        return;
    }

    wl_list_remove(&view->link);
    if (workspace) {
        wl_list_insert(&workspace->views, &view->link);
        /* reparent view tree to new workspace tree */
        enum layer layer = view->base.kept_above
                               ? LAYER_ABOVE
                               : (view->base.kept_below ? LAYER_BELOW : LAYER_NORMAL);
        struct view_layer *view_layer = workspace_layer(workspace, layer);
        ky_scene_node_reparent(ky_scene_node_from_tree(view->tree), view_layer->tree);
    } else {
        wl_list_init(&view->link);
    }

    kywc_log(KYWC_DEBUG, "kywc_view %p worskpace: %s", &view->base,
             workspace ? workspace->name : "none");

    view->workspace = workspace;
    wl_signal_emit_mutable(&view->events.workspace, NULL);
}

void view_set_parent(struct view *view, struct view *parent)
{
    if (view->parent == parent) {
        return;
    }

    wl_list_remove(&view->parent_link);
    if (parent) {
        wl_list_insert(&parent->children, &view->parent_link);
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

    /* it is not allowed to close a view that has children views */
    if (!wl_list_empty(&view->children)) {
        kywc_log(KYWC_WARN, "close a view that still have children views");
        return;
    }

    if (view->impl->close) {
        view->impl->close(view);
    }
}

void kywc_view_move(struct kywc_view *kywc_view, int x, int y)
{
    struct view *view = view_from_kywc_view(kywc_view);

    view->pending.action |= VIEW_ACTION_MOVE;
    view->pending.geometry.x = x;
    view->pending.geometry.y = y;

    if (kywc_view->mapped && view->impl->configure) {
        view->impl->configure(view);
    }
}

void kywc_view_resize(struct kywc_view *kywc_view, struct kywc_box *geometry)
{
    struct view *view = view_from_kywc_view(kywc_view);

    kywc_view->resizing = true;
    view->pending.action |= VIEW_ACTION_RESIZE;
    view->pending.geometry = *geometry;

    if (kywc_view->mapped && view->impl->configure) {
        view->impl->configure(view);
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
        wl_signal_add(&kywc_view->events.destroy, &view_manager->activated.unmap);
    } else {
        wl_list_remove(&view_manager->activated.minimize.link);
        wl_list_remove(&view_manager->activated.unmap.link);
        view_manager->activated.view = NULL;
    }

    /* change fullscreen layer when activation changed */
    if (kywc_view->fullscreen) {
        view_reparent_fullscreen(view, activated);
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
}

void view_topmost_activate(struct workspace *workspace)
{
    struct view *view;
    /* find topmost enabled(mapped and not minimized) view and activate it */
    wl_list_for_each(view, &workspace->views, link) {
        if (!view->base.activatable || !view->base.mapped || view->base.minimized) {
            continue;
        }
        kywc_view_activate(&view->base);
        seat_focus_surface(input_manager_get_default_seat(), view->surface);
        if (view->base.fullscreen) {
            view_reparent_fullscreen(view, true);
        }
        return;
    }

    /* workaround to hide fullscreen view when no view in workspace */
    view = view_manager->activated.view;
    if (view && view->base.fullscreen && !view->workspace->activated) {
        view_reparent_fullscreen(view, false);
        view_set_activated(view, false);
    }

    /* no view can be activated, clear keyboard focus */
    seat_focus_surface(input_manager_get_default_seat(), NULL);
}

void kywc_view_activate(struct kywc_view *kywc_view)
{
    struct view *view = view_from_kywc_view(kywc_view);
    struct view *last = view_manager->activated.view;
    if (!view->base.activatable || last == view) {
        return;
    }

    if (last) {
        view_set_activated(last, false);
    }

    view_set_activated(view, true);

    /* insert view in workspace topmost */
    if (view->workspace) {
        wl_list_remove(&view->link);
        wl_list_insert(&view->workspace->views, &view->link);
    }

    if (view->parent) {
        ky_scene_node_raise_to_top(ky_scene_node_from_tree(view->parent->tree));
    }
    ky_scene_node_raise_to_top(ky_scene_node_from_tree(view->tree));
    /* raise children if any */
    struct view *child;
    wl_list_for_each(child, &view->children, parent_link) {
        ky_scene_node_raise_to_top(ky_scene_node_from_tree(child->tree));
    }
}

void kywc_view_set_tiled(struct kywc_view *kywc_view, enum kywc_tile tile,
                         struct kywc_output *kywc_output)
{
    struct view *view = view_from_kywc_view(kywc_view);

    /* tiled mode may switch between outputs */
    if (kywc_view->tiled == tile && (!kywc_output || kywc_output == view->output)) {
        return;
    }

    struct kywc_box geo = { 0 };
    view_get_tiled_geometry(view, &geo, kywc_output ? kywc_output : view->output, tile);

    /* may switch between tiled modes */
    if (kywc_view->tiled == KYWC_TILE_NONE && tile != KYWC_TILE_NONE) {
        view->saved.geometry = view->base.geometry;
    }

    kywc_view->tiled = tile;
    view->pending.action |= VIEW_ACTION_TILE;
    view->pending.geometry = geo;

    if (kywc_view->mapped && view->impl->configure) {
        view->impl->configure(view);
    }
}

void kywc_view_set_minimized(struct kywc_view *kywc_view, bool minimized)
{
    if (!kywc_view->minimizable || kywc_view->minimized == minimized) {
        return;
    }

    struct view *view = view_from_kywc_view(kywc_view);
    ky_scene_node_set_enabled(ky_scene_node_from_tree(view->tree), !minimized);

    kywc_view->minimized = minimized;
    view->pending.action |= VIEW_ACTION_MINIMIZE;

    if (kywc_view->mapped && view->impl->configure) {
        view->impl->configure(view);
    }

    /* if view is the activated view, process it in activated.minimize listener */
    wl_signal_emit_mutable(&kywc_view->events.minimize, NULL);
}

void kywc_view_toggle_minimized(struct kywc_view *kywc_view)
{
    kywc_view_set_minimized(kywc_view, !kywc_view->minimized);
}

void kywc_view_set_maximized(struct kywc_view *kywc_view, bool maximized,
                             struct kywc_output *kywc_output)
{
    struct view *view = view_from_kywc_view(kywc_view);

    /* tiled to unmaximized after tiled from maximized */
    if (!kywc_view->maximizable || (!kywc_view->tiled && kywc_view->maximized == maximized &&
                                    (!kywc_output || kywc_output == view->output))) {
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
        /* don't restore tiled mode followed other compositors */
        if (kywc_view->tiled) {
            view->pending.action |= VIEW_ACTION_TILE;
            kywc_view->tiled = KYWC_TILE_NONE;
        }
        geo = view->saved.geometry;
    }

    kywc_view->maximized = maximized;
    view->pending.action |= VIEW_ACTION_MAXIMIZE;
    view->pending.geometry = geo;

    if (kywc_view->mapped && view->impl->configure) {
        view->impl->configure(view);
    }
}

void kywc_view_toggle_maximized(struct kywc_view *kywc_view)
{
    kywc_view_set_maximized(kywc_view, !kywc_view->maximized, NULL);
}

static void view_reparent_fullscreen(struct view *view, bool active)
{
    struct kywc_view *kywc_view = &view->base;

    if (active) {
        view->saved.layer = kywc_view->kept_above
                                ? LAYER_ABOVE
                                : (kywc_view->kept_below ? LAYER_BELOW : LAYER_NORMAL);
        struct view_layer *layer = view_manager_get_layer(LAYER_ACTIVE, false);
        ky_scene_node_reparent(ky_scene_node_from_tree(view->tree), layer->tree);
        return;
    }

    /* restore fullscreen view to workspace */
    struct view_layer *layer = workspace_layer(view->workspace, view->saved.layer);
    ky_scene_node_reparent(ky_scene_node_from_tree(view->tree), layer->tree);
}

void kywc_view_set_fullscreen(struct kywc_view *kywc_view, bool fullscreen,
                              struct kywc_output *kywc_output)
{
    struct view *view = view_from_kywc_view(kywc_view);

    if (!kywc_view->fullscreenable ||
        (kywc_view->fullscreen == fullscreen && (!kywc_output || kywc_output == view->output))) {
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

void kywc_view_toggle_fullscreen(struct kywc_view *kywc_view)
{
    kywc_view_set_fullscreen(kywc_view, !kywc_view->fullscreen, NULL);
}

void kywc_view_set_kept_above(struct kywc_view *kywc_view, bool kept_above)
{
    if (kywc_view->kept_above == kept_above) {
        return;
    }

    struct view *view = view_from_kywc_view(kywc_view);

    kywc_view->kept_above = kept_above;
    kywc_view->kept_below = false;

    enum layer layer = kept_above ? LAYER_ABOVE : LAYER_NORMAL;
    struct view_layer *view_layer = workspace_layer(workspace_manager_get_current(), layer);
    ky_scene_node_reparent(ky_scene_node_from_tree(view->tree), view_layer->tree);
}

void kywc_view_toggle_kept_above(struct kywc_view *kywc_view)
{
    kywc_view_set_kept_above(kywc_view, !kywc_view->kept_above);
}

void kywc_view_set_kept_below(struct kywc_view *kywc_view, bool kept_below)
{
    if (kywc_view->kept_below == kept_below) {
        return;
    }

    struct view *view = view_from_kywc_view(kywc_view);

    kywc_view->kept_below = kept_below;
    kywc_view->kept_above = false;

    enum layer layer = kept_below ? LAYER_BELOW : LAYER_NORMAL;
    struct view_layer *view_layer = workspace_layer(workspace_manager_get_current(), layer);
    ky_scene_node_reparent(ky_scene_node_from_tree(view->tree), view_layer->tree);
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
     * the postion will not change as the right one become (0,0).
     */
    view_update_output(view);

    if (changed) {
        ky_scene_node_set_position(ky_scene_node_from_tree(view->tree), x, y);
        wl_signal_emit_mutable(&view->base.events.position, NULL);
    }
}

bool view_is_moveable(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    return kywc_view->movable && !kywc_view->fullscreen;
}

bool view_is_resizable(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    return kywc_view->resizable && !kywc_view->fullscreen && !kywc_view->maximized;
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

void view_show_window_menu(struct view *view, struct seat *seat, int x, int y)
{
    struct view_show_window_menu_event event = { view, seat, x, y };
    wl_signal_emit_mutable(&view_manager->events.window_menu, &event);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&view_manager->server_destroy.link);

    free(view_manager);
    view_manager = NULL;
}

struct view_manager *view_manager_create(struct server *server)
{
    view_manager = calloc(1, sizeof(struct view_manager));
    if (!view_manager) {
        return NULL;
    }

    view_manager->server = server;
    wl_signal_init(&view_manager->events.new_view);
    wl_signal_init(&view_manager->events.window_menu);

    view_manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &view_manager->server_destroy);

    view_manager->activated.minimize.notify = handle_activated_view_minimized;
    view_manager->activated.unmap.notify = handle_activated_view_unmap;

    /* create all layers */
    for (int layer = LAYER_FIRST; layer < LAYER_NUMBER; layer++) {
        view_manager->layers[layer].layer = layer;
        view_manager->layers[layer].tree = ky_scene_create_tree(server->scene);
    }

    workspace_manager_create(view_manager);
    decoration_manager_create(view_manager);
    server_decoration_manager_create(view_manager);
    positioner_manager_create(view_manager);
    shadow_manager_create(view_manager);
    window_actions_create(view_manager);
    window_menu_manager_create(view_manager);

    xdg_shell_init(view_manager);

    wlr_layer_shell_manager_create(server);
    wlr_foreign_toplevel_manager_create(server);
    kde_plasma_shell_create(server);
    kde_plasma_window_management_create(server);
    // kde_blur_manager_create(server);

    return view_manager;
}
