// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "input/event.h"
#include "output.h"
#include "scene/xdg_shell.h"
#include "view_p.h"

struct xdg_popup {
    struct wlr_xdg_popup *wlr_xdg_popup;

    struct ky_scene_tree *popup_tree;
    /* may a view/layer tree or popup tree */
    struct ky_scene_tree *parent_tree;
    /* shell tree xdg-shell or layer-shell */
    struct ky_scene_tree *shell_tree;

    struct wl_listener destroy;
    struct wl_listener new_popup;

    bool topmost_popup;
};

static void handle_xdg_popup_destroy(struct wl_listener *listener, void *data)
{
    struct xdg_popup *popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->new_popup.link);

    /* only need to destroy the topmost popup parent tree,
     * popup tree will be destroyed by xdg_surface destroy in scene
     */
    if (popup->topmost_popup) {
        ky_scene_node_destroy(ky_scene_node_from_tree(popup->parent_tree));
    }

    free(popup);
}

static struct xdg_popup *_xdg_popup_create(struct wlr_xdg_popup *wlr_xdg_popup,
                                           struct ky_scene_tree *parent,
                                           struct ky_scene_tree *shell);

static void popup_handle_new_xdg_popup(struct wl_listener *listener, void *data)
{
    struct xdg_popup *popup = wl_container_of(listener, popup, new_popup);
    struct wlr_xdg_popup *wlr_popup = data;

    _xdg_popup_create(wlr_popup, popup->popup_tree, popup->shell_tree);
}

static void popup_unconstrain(struct xdg_popup *popup)
{
    /* TODO: popup unconstrain output, add input_manager_get_last_seat */
    struct output *output = input_current_output(input_manager_get_default_seat());
    struct kywc_box *output_box = &output->geometry;

    int lx = 0, ly = 0;
    ky_scene_node_coords(ky_scene_node_from_tree(popup->shell_tree), &lx, &ly);

    struct wlr_box toplevel_space_box = {
        .x = output_box->x - lx,
        .y = output_box->y - ly,
        .width = output_box->width,
        .height = output_box->height,
    };

    wlr_xdg_popup_unconstrain_from_box(popup->wlr_xdg_popup, &toplevel_space_box);
}

static struct xdg_popup *_xdg_popup_create(struct wlr_xdg_popup *wlr_xdg_popup,
                                           struct ky_scene_tree *parent,
                                           struct ky_scene_tree *shell)
{
    struct xdg_popup *popup = calloc(1, sizeof(struct xdg_popup));
    if (!popup) {
        return NULL;
    }

    popup->wlr_xdg_popup = wlr_xdg_popup;
    popup->parent_tree = parent;
    popup->shell_tree = shell;

    /* add popup surface to view tree, popup map and unmap is handled in scene */
    popup->popup_tree = ky_scene_xdg_surface_create(parent, wlr_xdg_popup->base);

    popup->destroy.notify = handle_xdg_popup_destroy;
    wl_signal_add(&wlr_xdg_popup->base->events.destroy, &popup->destroy);
    popup->new_popup.notify = popup_handle_new_xdg_popup;
    wl_signal_add(&wlr_xdg_popup->base->events.new_popup, &popup->new_popup);

    popup_unconstrain(popup);

    return popup;
}

static bool xdg_popup_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                            uint32_t time, bool first, bool hold, void *data)
{
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    if (first) {
        kywc_log(KYWC_DEBUG, "first hover surface %p (%f %f)", surface, x, y);
    }

    seat_notify_motion(seat, surface, time, x, y, first);
    return false;
}

static void xdg_popup_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                            bool pressed, uint32_t time, bool dual, void *data)
{
    seat_notify_button(seat, time, button, pressed);
}

static void xdg_popup_leave(struct seat *seat, struct ky_scene_node *node, bool last, void *data)
{
    /* so surface will call set_cursor when enter again */
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    seat_notify_leave(seat, surface);
}

static struct ky_scene_node *xdg_popup_get_root(void *data)
{
    struct xdg_popup *popup = data;
    return ky_scene_node_from_tree(popup->parent_tree);
}

static const struct input_event_node_impl xdg_popup_event_node_impl = {
    .hover = xdg_popup_hover,
    .click = xdg_popup_click,
    .leave = xdg_popup_leave,
};

void xdg_popup_create(struct wlr_xdg_popup *wlr_xdg_popup, struct ky_scene_tree *shell)
{
    struct view_layer *popup_layer = view_manager_get_layer(LAYER_POPUP, false);
    struct ky_scene_tree *parent = ky_scene_tree_create(popup_layer->tree);

    /* get shell layout coord, and set it to parent tree */
    int lx, ly;
    ky_scene_node_coords(ky_scene_node_from_tree(shell), &lx, &ly);
    ky_scene_node_set_position(ky_scene_node_from_tree(parent), lx, ly);

    struct xdg_popup *popup = _xdg_popup_create(wlr_xdg_popup, parent, shell);
    popup->topmost_popup = true;

    input_event_node_create(ky_scene_node_from_tree(parent), &xdg_popup_event_node_impl,
                            xdg_popup_get_root, NULL, popup);
}
