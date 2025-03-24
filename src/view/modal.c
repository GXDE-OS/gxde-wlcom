// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include "effect/node_transform.h"
#include "effect/shake_view.h"
#include "input/event.h"
#include "scene/surface.h"
#include "theme.h"
#include "util/time.h"
#include "view/workspace.h"
#include "view_p.h"

static float modal_color[4] = { 18.0 / 255, 18.0 / 255, 18.0 / 255, 51.0 / 255 };

struct modal {
    struct view *view;
    struct ky_scene_rect *modal_box;

    struct kywc_box geo;

    struct wl_listener view_unmap;
    struct wl_listener unset_modal;

    struct wl_listener parent_unmap;
    struct wl_listener parent_size;
};

static bool modal_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                        uint32_t time, bool first, bool hold, void *data)
{
    return false;
}

static void modal_leave(struct seat *seat, struct ky_scene_node *node, bool last, void *data) {}

static void modal_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                        bool pressed, uint32_t time, enum click_state state, void *data)
{
    if (!pressed) {
        return;
    }

    struct modal *modal = data;

    struct view *descendant = view_find_descendant_modal(modal->view);
    modal = descendant ? descendant->modal : modal;
    /* active current view */
    kywc_view_activate(&modal->view->base);
    view_set_focus(modal->view, seat);

    view_add_shake_effect(modal->view);
}

static struct ky_scene_node *modal_get_root(void *data)
{
    struct modal *modal = data;
    return &modal->modal_box->node;
}

static const struct input_event_node_impl modal_impl = {
    .hover = modal_hover,
    .leave = modal_leave,
    .click = modal_click,
};

static void modal_add_rect_opacity_transform(struct ky_scene_node *node, int duration,
                                             struct kywc_box geo, float start_alpha,
                                             float end_alpha, int64_t start_time,
                                             enum animation_type alpha_type)
{
    struct transform_options options = { 0 };
    options.start_time = start_time;
    options.type.alpha = alpha_type;
    options.duration = duration;
    options.start.geometry = options.end.geometry = geo;
    options.start.alpha = start_alpha;
    options.end.alpha = end_alpha;
    node_add_transform_effect(node, &options);
}

static void modal_destroy(struct modal *modal)
{
    modal->view->modal = NULL;
    wl_list_remove(&modal->parent_unmap.link);
    wl_list_remove(&modal->parent_size.link);
    wl_list_remove(&modal->unset_modal.link);
    wl_list_remove(&modal->view_unmap.link);

    modal_add_rect_opacity_transform(&modal->modal_box->node, 200, modal->geo, 1.0, 0,
                                     current_time_msec(), ANIMATION_TYPE_EASE);

    ky_scene_node_destroy(&modal->modal_box->node);
    free(modal);
}

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct modal *modal = wl_container_of(listener, modal, view_unmap);
    modal_destroy(modal);
}

static void handle_parent_unmap(struct wl_listener *listener, void *data)
{
    struct modal *modal = wl_container_of(listener, modal, parent_unmap);
    modal_destroy(modal);
}

static void modal_box_set_round_corner(struct ky_scene_rect *modal_box, struct view *view);

static void handle_parent_size(struct wl_listener *listener, void *data)
{
    struct modal *modal = wl_container_of(listener, modal, parent_size);
    struct kywc_view *parent = &modal->view->parent->base;
    struct kywc_box geo = {
        .x = -parent->margin.off_x,
        .y = -parent->margin.off_y,
        .width = parent->geometry.width + parent->margin.off_width,
        .height = parent->geometry.height + parent->margin.off_height,
    };

    struct kywc_box geometry = { parent->geometry.x + geo.x, parent->geometry.y + geo.y, geo.width,
                                 geo.height };
    modal->geo = geometry;
    ky_scene_rect_set_size(modal->modal_box, geo.width, geo.height);
    ky_scene_node_set_position(&modal->modal_box->node, geo.x, geo.y);
    modal_box_set_round_corner(modal->modal_box, modal->view->parent);
}

static void handle_unset_modal(struct wl_listener *listener, void *data)
{
    struct modal *modal = wl_container_of(listener, modal, unset_modal);
    modal_destroy(modal);
}

static void modal_box_set_round_corner(struct ky_scene_rect *modal_box, struct view *view)
{
    struct ky_scene_buffer *buffer = ky_scene_buffer_try_from_surface(view->surface);
    if (!buffer) {
        return;
    }

    int radius[4] = { 0 };
    memcpy(radius, buffer->node.radius, sizeof(radius));

    /* set top corner if has ssd title */
    struct kywc_view *kywc_view = &view->base;
    if (kywc_view->ssd & KYWC_SSD_TITLE) {
        bool need_corner = !kywc_view->maximized && !kywc_view->fullscreen && !kywc_view->tiled;
        struct theme *theme = theme_manager_get_theme();
        radius[KY_SCENE_ROUND_CORNER_RT] = need_corner ? theme->corner_radius : 0;
        radius[KY_SCENE_ROUND_CORNER_LT] = need_corner ? theme->corner_radius : 0;
    }

    ky_scene_node_set_radius(&modal_box->node, radius);
}

static void modal_add_parent_workspace(struct view *view)
{
    if (!view || !view->parent) {
        return;
    }
    struct view *parent = view->parent;
    struct view_proxy *proxy;
    wl_list_for_each(proxy, &parent->view_proxies, view_link) {
        view_add_workspace(view, proxy->workspace);
    }
}

void modal_create(struct view *view)
{
    if (!view->base.modal || !view->parent || !view->parent->surface) {
        return;
    }

    struct modal *modal = calloc(1, sizeof(struct modal));
    if (!modal) {
        return;
    }

    modal->view = view;
    view->modal = modal;
    struct kywc_view *parent = &view->parent->base;
    struct kywc_box geo = {
        .x = -parent->margin.off_x,
        .y = -parent->margin.off_y,
        .width = parent->geometry.width + parent->margin.off_width,
        .height = parent->geometry.height + parent->margin.off_height,
    };

    modal->modal_box = ky_scene_rect_create(view->parent->tree, geo.width, geo.height, modal_color);
    ky_scene_node_raise_to_top(&modal->modal_box->node);
    ky_scene_node_set_position(&modal->modal_box->node, geo.x, geo.y);
    modal_box_set_round_corner(modal->modal_box, view->parent);

    struct kywc_box geometry = { parent->geometry.x + geo.x, parent->geometry.y + geo.y, geo.width,
                                 geo.height };
    modal->geo = geometry;
    modal_add_rect_opacity_transform(&modal->modal_box->node, 300, modal->geo, 0, 1.0,
                                     current_time_msec(), ANIMATION_TYPE_EASE);

    input_event_node_create(&modal->modal_box->node, &modal_impl, modal_get_root, NULL, modal);

    modal_add_parent_workspace(view);

    modal->view_unmap.notify = handle_view_unmap;
    wl_signal_add(&view->base.events.unmap, &modal->view_unmap);
    modal->unset_modal.notify = handle_unset_modal;
    wl_signal_add(&view->base.events.unset_modal, &modal->unset_modal);

    modal->parent_unmap.notify = handle_parent_unmap;
    wl_signal_add(&parent->events.unmap, &modal->parent_unmap);
    modal->parent_size.notify = handle_parent_size;
    wl_signal_add(&parent->events.size, &modal->parent_size);
}
