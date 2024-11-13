// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include "effect/shake_view.h"
#include "input/event.h"
#include "output.h"
#include "view_p.h"

static float authentication_color[4] = { 18.0 / 255, 18.0 / 255, 18.0 / 255, 128.0 / 255 };

struct global_authentication {
    struct view *view;
    struct ky_scene_rect *box;
    struct ky_scene_tree *tree;

    struct wl_listener view_unmap;
    struct wl_listener configured_output;
};

static bool authentication_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                                 uint32_t time, bool first, bool hold, void *data)
{
    return false;
}

static void authentication_leave(struct seat *seat, struct ky_scene_node *node, bool last,
                                 void *data)
{
}

static void authentication_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                                 bool pressed, uint32_t time, enum click_state state, void *data)
{
    if (!pressed) {
        return;
    }

    struct global_authentication *authentication = data;
    /* active current view */
    kywc_view_activate(&authentication->view->base);
    view_add_shake_effect(authentication->view);
}

static struct ky_scene_node *authentication_get_root(void *data)
{
    struct global_authentication *authentication = data;
    return &authentication->box->node;
}

static const struct input_event_node_impl authentication_impl = {
    .hover = authentication_hover,
    .leave = authentication_leave,
    .click = authentication_click,
};

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct global_authentication *authentication =
        wl_container_of(listener, authentication, view_unmap);
    wl_list_remove(&authentication->view_unmap.link);
    wl_list_remove(&authentication->configured_output.link);

    ky_scene_node_destroy(&authentication->tree->node);
    free(authentication);
}

static void handle_configured_output(struct wl_listener *listener, void *data)
{
    struct global_authentication *authentication =
        wl_container_of(listener, authentication, configured_output);
    int width = 0, height = 0;
    output_layout_get_size(&width, &height);
    ky_scene_rect_set_size(authentication->box, width, height);
}

void global_authentication_create(struct view *view)
{
    struct global_authentication *authentication = calloc(1, sizeof(struct global_authentication));
    if (!authentication) {
        return;
    }

    authentication->view = view;
    struct view_layer *layer = view_manager_get_layer(LAYER_CRITICAL_NOTIFICATION, false);
    authentication->tree = ky_scene_tree_create(layer->tree);
    int width = 0, height = 0;
    output_layout_get_size(&width, &height);
    authentication->box =
        ky_scene_rect_create(authentication->tree, width, height, authentication_color);
    ky_scene_node_lower_to_bottom(&authentication->tree->node);

    input_event_node_create(&authentication->box->node, &authentication_impl,
                            authentication_get_root, NULL, authentication);

    authentication->view_unmap.notify = handle_view_unmap;
    wl_signal_add(&view->base.events.unmap, &authentication->view_unmap);
    authentication->configured_output.notify = handle_configured_output;
    output_manager_add_configured_listener(&authentication->configured_output);
}