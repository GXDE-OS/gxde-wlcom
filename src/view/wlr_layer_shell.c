// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_seat.h>

#include "input/event.h"
#include "input/seat.h"
#include "output.h"
#include "scene/surface.h"
#include "view/workspace.h"
#include "view_p.h"

struct wlr_layer_shell_manager {
    struct wl_list outputs;

    /* layers for layer shell  */
    struct view_layer layers[4];

    struct wl_listener new_output;
    struct wl_listener new_surface;
    struct wl_listener destroy;
};

struct layer_output {
    struct wl_list link;
    struct output *output;

    /* layer shells in 4 layers */
    struct wl_list shells[4];

    /*  if have exclusive_zone < 0 */
    struct wl_listener geometry;
    /*  if have exclusive_zone == 0 */
    struct wl_listener usable_area;
    /* if have exclusive_zone > 0 */
    struct wl_listener update_usable_area;
    /* destroy wlr layer-shell if output off and destroy*/
    struct wl_listener off;
    struct wl_listener destroy;
};

struct layer_shell {
    struct wl_list link;

    struct ky_scene_tree *tree;
    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_seat_keyboard_grab keyboard_grab;

    struct wl_listener commit;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener new_popup;
    struct wl_listener destroy;
};

static struct wlr_layer_shell_manager *manager = NULL;

static void layer_shell_keyboard_enter(struct wlr_seat_keyboard_grab *grab,
                                       struct wlr_surface *surface, const uint32_t keycodes[],
                                       size_t num_keycodes,
                                       const struct wlr_keyboard_modifiers *modifiers)
{
    // keyboard focus should remain on the layer shell
}

static void layer_shell_keyboard_clear_focus(struct wlr_seat_keyboard_grab *grab)
{
    // keyboard focus should remain on the layer shell
}

static void layer_shell_keyboard_key(struct wlr_seat_keyboard_grab *grab, uint32_t time,
                                     uint32_t key, uint32_t state)
{
    wlr_seat_keyboard_send_key(grab->seat, time, key, state);
}

static void layer_shell_keyboard_modifiers(struct wlr_seat_keyboard_grab *grab,
                                           const struct wlr_keyboard_modifiers *modifiers)
{
    wlr_seat_keyboard_send_modifiers(grab->seat, modifiers);
}

static void layer_shell_keyboard_cancel(struct wlr_seat_keyboard_grab *grab)
{
    grab->seat = NULL;
}

static const struct wlr_keyboard_grab_interface layer_shell_keyboard_grab = {
    .enter = layer_shell_keyboard_enter,
    .clear_focus = layer_shell_keyboard_clear_focus,
    .key = layer_shell_keyboard_key,
    .modifiers = layer_shell_keyboard_modifiers,
    .cancel = layer_shell_keyboard_cancel,
};

static void layer_shell_keyboard_interactivity(struct layer_shell *layer_shell, struct seat *seat)
{
    struct wlr_layer_surface_v1 *layer_surface = layer_shell->layer_surface;

    if (layer_surface->surface->mapped) {
        if (layer_surface->current.keyboard_interactive) {
            seat_focus_surface(seat, layer_surface->surface);
            /* start a seat keyboard grab */
            if (layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
                layer_shell->keyboard_grab.interface = &layer_shell_keyboard_grab;
                layer_shell->keyboard_grab.seat = seat->wlr_seat;
                layer_shell->keyboard_grab.data = layer_shell;
                wlr_seat_keyboard_start_grab(seat->wlr_seat, &layer_shell->keyboard_grab);
            }
        }
    } else {
        struct wlr_seat *wlr_seat = layer_shell->keyboard_grab.seat;
        if (wlr_seat && wlr_seat->keyboard_state.grab == &layer_shell->keyboard_grab) {
            wlr_seat_keyboard_end_grab(wlr_seat);
        }
        // XXX: auto focus layer_shell
        if (seat->wlr_seat->keyboard_state.focused_surface == layer_surface->surface) {
            view_topmost_activate(workspace_manager_get_current());
        }
    }
}

static struct layer_output *layer_output_from_wlr_output(struct wlr_output *wlr_output)
{
    struct layer_output *layer_output;
    wl_list_for_each(layer_output, &manager->outputs, link) {
        if (layer_output->output->wlr_output == wlr_output) {
            return layer_output;
        }
    }

    return NULL;
}

static void layer_surface_exclusive_zone(struct wlr_layer_surface_v1_state *state,
                                         struct kywc_box *usable_area)
{
    switch (state->anchor) {
    case ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP:
    case (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
          ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT):
        // Anchor top
        usable_area->y += state->exclusive_zone + state->margin.top;
        usable_area->height -= state->exclusive_zone + state->margin.top;
        break;
    case ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM:
    case (ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
          ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT):
        // Anchor bottom
        usable_area->height -= state->exclusive_zone + state->margin.bottom;
        break;
    case ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT:
    case (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
          ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT):
        // Anchor left
        usable_area->x += state->exclusive_zone + state->margin.left;
        usable_area->width -= state->exclusive_zone + state->margin.left;
        break;
    case ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT:
    case (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
          ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT):
        // Anchor right
        usable_area->width -= state->exclusive_zone + state->margin.right;
        break;
    }
}

static void layer_shell_configure_surface(struct layer_shell *layer_shell,
                                          const struct kywc_box *full_area,
                                          struct kywc_box *usable_area)
{
    struct wlr_layer_surface_v1 *layer_surface = layer_shell->layer_surface;
    struct wlr_layer_surface_v1_state *state = &layer_surface->current;

    // If the exclusive zone is set to -1, the layer surface will use the
    // full area of the output, otherwise it is constrained to the
    // remaining usable area.
    struct kywc_box bounds;
    if (state->exclusive_zone == -1) {
        bounds = *full_area;
    } else {
        bounds = *usable_area;
    }

    struct kywc_box box = {
        .width = state->desired_width,
        .height = state->desired_height,
    };

    // Horizontal positioning
    if (box.width == 0) {
        box.x = bounds.x + state->margin.left;
        box.width = bounds.width - (state->margin.left + state->margin.right);
    } else if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT &&
               state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) {
        box.x = bounds.x + bounds.width / 2 - box.width / 2;
    } else if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) {
        box.x = bounds.x + state->margin.left;
    } else if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) {
        box.x = bounds.x + bounds.width - box.width - state->margin.right;
    } else {
        box.x = bounds.x + bounds.width / 2 - box.width / 2;
    }

    // Vertical positioning
    if (box.height == 0) {
        box.y = bounds.y + state->margin.top;
        box.height = bounds.height - (state->margin.top + state->margin.bottom);
    } else if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP &&
               state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) {
        box.y = bounds.y + bounds.height / 2 - box.height / 2;
    } else if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) {
        box.y = bounds.y + state->margin.top;
    } else if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) {
        box.y = bounds.y + bounds.height - box.height - state->margin.bottom;
    } else {
        box.y = bounds.y + bounds.height / 2 - box.height / 2;
    }

    ky_scene_node_set_position(ky_scene_node_from_tree(layer_shell->tree), box.x, box.y);
    wlr_layer_surface_v1_configure(layer_surface, box.width, box.height);

    if (layer_surface->surface->mapped && state->exclusive_zone > 0) {
        layer_surface_exclusive_zone(state, usable_area);
    }
}

static void layer_shell_handle_commit(struct wl_listener *listener, void *data)
{
    struct layer_shell *layer_shell = wl_container_of(listener, layer_shell, commit);
    struct wlr_layer_surface_v1 *layer_surface = layer_shell->layer_surface;

    if (!layer_surface->output) {
        return;
    }

    struct output *output = output_from_wlr_output(layer_surface->output);

    if (!layer_surface->surface->mapped) {
        /* is not mapped, usable area will not be changed */
        layer_shell_configure_surface(layer_shell, &output->geometry, &output->usable_area);
        return;
    }

    uint32_t committed = layer_surface->current.committed;

    if (committed & WLR_LAYER_SURFACE_V1_STATE_LAYER) {
        ky_scene_node_reparent(ky_scene_node_from_tree(layer_shell->tree),
                               manager->layers[layer_surface->current.layer].tree);
        committed &= ~WLR_LAYER_SURFACE_V1_STATE_LAYER;
    }

    if (committed & WLR_LAYER_SURFACE_V1_STATE_KEYBOARD_INTERACTIVITY) {
        layer_shell_keyboard_interactivity(layer_shell, input_manager_get_default_seat());
        committed &= ~WLR_LAYER_SURFACE_V1_STATE_KEYBOARD_INTERACTIVITY;
    }

    if (committed) {
        kywc_output_update_usable_area(&output->base);
    }
}

static void layer_shell_handle_map(struct wl_listener *listener, void *data)
{
    struct layer_shell *layer_shell = wl_container_of(listener, layer_shell, map);
    struct wlr_layer_surface_v1 *layer_surface = layer_shell->layer_surface;

    /* layer-shell first configure is done in commit */
    ky_scene_node_set_enabled(ky_scene_node_from_tree(layer_shell->tree), true);

    if (layer_surface->current.exclusive_zone > 0) {
        struct output *output = output_from_wlr_output(layer_surface->output);
        kywc_output_update_usable_area(&output->base);
    }

    layer_shell_keyboard_interactivity(layer_shell, input_manager_get_default_seat());
}

/* called when precommit */
static void layer_shell_handle_unmap(struct wl_listener *listener, void *data)
{
    struct layer_shell *layer_shell = wl_container_of(listener, layer_shell, unmap);
    struct wlr_layer_surface_v1 *layer_surface = layer_shell->layer_surface;

    ky_scene_node_set_enabled(ky_scene_node_from_tree(layer_shell->tree), false);

    if (!layer_surface->output) {
        return;
    }

    layer_shell_keyboard_interactivity(layer_shell, input_manager_get_default_seat());

    if (layer_surface->current.exclusive_zone > 0) {
        kywc_output_update_usable_area(&output_from_wlr_output(layer_surface->output)->base);
    }
}

static void layer_shell_handle_destroy(struct wl_listener *listener, void *data)
{
    struct layer_shell *layer_shell = wl_container_of(listener, layer_shell, destroy);

    wl_list_remove(&layer_shell->destroy.link);
    wl_list_remove(&layer_shell->commit.link);
    wl_list_remove(&layer_shell->map.link);
    wl_list_remove(&layer_shell->unmap.link);
    wl_list_remove(&layer_shell->new_popup.link);
    wl_list_remove(&layer_shell->link);

    ky_scene_node_destroy(ky_scene_node_from_tree(layer_shell->tree));

    free(layer_shell);
}

static void layer_shell_handle_new_popup(struct wl_listener *listener, void *data)
{
    struct layer_shell *layer_shell = wl_container_of(listener, layer_shell, new_popup);
    struct wlr_xdg_popup *wlr_xdg_popup = data;

    xdg_popup_create(wlr_xdg_popup, layer_shell->tree);
}

static bool layer_shell_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                              uint32_t time, bool first, bool hold, void *data)
{
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    if (first) {
        kywc_log(KYWC_DEBUG, "first hover surface %p (%f %f)", surface, x, y);
    }

    seat_notify_motion(seat, surface, time, x, y, first);
    return false;
}

static void layer_shell_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                              bool pressed, uint32_t time, bool dual, void *data)
{
    struct layer_shell *layer_shell = data;
    seat_notify_button(seat, time, button, pressed);
    layer_shell_keyboard_interactivity(layer_shell, seat);
}

static void layer_shell_leave(struct seat *seat, struct ky_scene_node *node, bool last, void *data)
{
    /* so surface will call set_cursor when enter again */
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    seat_notify_leave(seat, surface);
}

static struct ky_scene_node *layer_shell_get_root(void *data)
{
    struct layer_shell *layer_shell = data;
    return ky_scene_node_from_tree(layer_shell->tree);
}

static struct wlr_surface *layer_shell_get_toplevel(void *data)
{
    struct layer_shell *layer_shell = data;
    return layer_shell->layer_surface->surface;
}

static const struct input_event_node_impl layer_shell_event_node_impl = {
    .hover = layer_shell_hover,
    .click = layer_shell_click,
    .leave = layer_shell_leave,
};

static void handle_new_layer_surface(struct wl_listener *listener, void *data)
{
    struct wlr_layer_surface_v1 *layer_surface = data;

    /* the user most recently interacted with. */
    if (!layer_surface->output) {
        struct output *output = input_current_output(input_manager_get_default_seat());
        if (!output) {
            kywc_log(KYWC_WARN, "cannot find output for layer shell");
            wlr_layer_surface_v1_destroy(layer_surface);
            return;
        }
        layer_surface->output = output->wlr_output;
    }

    struct layer_shell *layer_shell = calloc(1, sizeof(struct layer_shell));
    if (!layer_shell) {
        return;
    }

    struct layer_output *layer_output = layer_output_from_wlr_output(layer_surface->output);
    wl_list_insert(&layer_output->shells[layer_surface->current.layer], &layer_shell->link);

    layer_shell->layer_surface = layer_surface;
    layer_shell->tree = ky_scene_tree_create(manager->layers[layer_surface->current.layer].tree);
    ky_scene_subsurface_tree_create(layer_shell->tree, layer_surface->surface);

    input_event_node_create(ky_scene_node_from_tree(layer_shell->tree),
                            &layer_shell_event_node_impl, layer_shell_get_root,
                            layer_shell_get_toplevel, layer_shell);

    layer_shell->commit.notify = layer_shell_handle_commit;
    wl_signal_add(&layer_surface->surface->events.commit, &layer_shell->commit);

    layer_shell->map.notify = layer_shell_handle_map;
    wl_signal_add(&layer_surface->surface->events.map, &layer_shell->map);
    layer_shell->unmap.notify = layer_shell_handle_unmap;
    wl_signal_add(&layer_surface->surface->events.unmap, &layer_shell->unmap);
    layer_shell->destroy.notify = layer_shell_handle_destroy;
    wl_signal_add(&layer_surface->events.destroy, &layer_shell->destroy);
    layer_shell->new_popup.notify = layer_shell_handle_new_popup;
    wl_signal_add(&layer_surface->events.new_popup, &layer_shell->new_popup);

    ky_scene_node_set_enabled(ky_scene_node_from_tree(layer_shell->tree),
                              layer_surface->surface->mapped);
}

static void layer_output_destroy_shells(struct layer_output *layer_output)
{
    struct layer_shell *layer_shell, *tmp;

    for (int i = 0; i < 4; i++) {
        wl_list_for_each_safe(layer_shell, tmp, &layer_output->shells[i], link) {
            wl_list_remove(&layer_shell->link);
            wl_list_init(&layer_shell->link);
            /* mark output is null, skip update output when unmap */
            layer_shell->layer_surface->output = NULL;
            wlr_layer_surface_v1_destroy(layer_shell->layer_surface);
        }
    }
}

static void handle_output_off(struct wl_listener *listener, void *data)
{
    struct layer_output *layer_output = wl_container_of(listener, layer_output, off);

    layer_output_destroy_shells(layer_output);
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct layer_output *layer_output = wl_container_of(listener, layer_output, destroy);

    /* output is enabled when destroy */
    if (layer_output->output->base.state.enabled) {
        layer_output_destroy_shells(layer_output);
    }

    wl_list_remove(&layer_output->destroy.link);
    wl_list_remove(&layer_output->geometry.link);
    wl_list_remove(&layer_output->usable_area.link);
    wl_list_remove(&layer_output->update_usable_area.link);
    wl_list_remove(&layer_output->off.link);
    wl_list_remove(&layer_output->link);

    free(layer_output);
}

static void handle_output_geometry(struct wl_listener *listener, void *data)
{
    struct layer_output *layer_output = wl_container_of(listener, layer_output, geometry);

    /* configure layer-shells that exclusive_zone < 0 */
    struct layer_shell *layer_shell;
    for (int i = 0; i < 4; i++) {
        wl_list_for_each(layer_shell, &layer_output->shells[i], link) {
            if (layer_shell->layer_surface->current.exclusive_zone >= 0) {
                continue;
            }
            layer_shell_configure_surface(layer_shell, &layer_output->output->geometry,
                                          &layer_output->output->usable_area);
        }
    }
}

static void handle_output_usable_area(struct wl_listener *listener, void *data)
{
    struct layer_output *layer_output = wl_container_of(listener, layer_output, usable_area);

    /* configure layer-shells that exclusive_zone == 0 */
    struct layer_shell *layer_shell;
    for (int i = 0; i < 4; i++) {
        wl_list_for_each(layer_shell, &layer_output->shells[i], link) {
            if (layer_shell->layer_surface->current.exclusive_zone != 0) {
                continue;
            }
            layer_shell_configure_surface(layer_shell, &layer_output->output->geometry,
                                          &layer_output->output->usable_area);
        }
    }
}

static void handle_output_update_usable_area(struct wl_listener *listener, void *data)
{
    struct layer_output *layer_output = wl_container_of(listener, layer_output, update_usable_area);

    struct kywc_box *usable_area = data;

    /* update usable area and configure layer-shells that exclusive_zone > 0 */
    struct layer_shell *layer_shell;
    for (int i = 0; i < 4; i++) {
        wl_list_for_each(layer_shell, &layer_output->shells[i], link) {
            if (layer_shell->layer_surface->current.exclusive_zone <= 0) {
                continue;
            }
            layer_shell_configure_surface(layer_shell, &layer_output->output->geometry,
                                          usable_area);
        }
    }
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct layer_output *layer_output = calloc(1, sizeof(struct layer_output));
    if (!layer_output) {
        return;
    }

    struct output *output = output_from_kywc_output(data);

    for (int i = 0; i < 4; i++) {
        wl_list_init(&layer_output->shells[i]);
    }
    wl_list_insert(&manager->outputs, &layer_output->link);

    layer_output->output = output;

    layer_output->off.notify = handle_output_off;
    wl_signal_add(&output->base.events.off, &layer_output->off);
    layer_output->destroy.notify = handle_output_destroy;
    wl_signal_add(&output->base.events.destroy, &layer_output->destroy);

    layer_output->geometry.notify = handle_output_geometry;
    wl_signal_add(&output->events.geometry, &layer_output->geometry);
    layer_output->usable_area.notify = handle_output_usable_area;
    wl_signal_add(&output->events.usable_area, &layer_output->usable_area);
    layer_output->update_usable_area.notify = handle_output_update_usable_area;
    output_add_update_usable_area_listener(&output->base, &layer_output->update_usable_area, true);
}

static void handle_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->destroy.link);
    free(manager);
    manager = NULL;
}

bool wlr_layer_shell_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(struct wlr_layer_shell_manager));
    if (!manager) {
        return false;
    }

    struct wlr_layer_shell_v1 *wlr_layer_shell = wlr_layer_shell_v1_create(server->display, 4);
    if (!wlr_layer_shell) {
        kywc_log(KYWC_WARN, "wlroots layer shell create failed");
        free(manager);
        return false;
    }

    wl_list_init(&manager->outputs);

    /* create bottom and top layer tree not in workspace */
    manager->layers[0] = *view_manager_get_layer(LAYER_DESKTOP, false);
    manager->layers[1] = *view_manager_get_layer(LAYER_BELOW, false);
    manager->layers[1].tree = ky_scene_tree_create(manager->layers[1].tree);
    manager->layers[2] = *view_manager_get_layer(LAYER_ABOVE, false);
    manager->layers[2].tree = ky_scene_tree_create(manager->layers[2].tree);
    manager->layers[3] = *view_manager_get_layer(LAYER_UNMANAGED, false);

    manager->destroy.notify = handle_destroy;
    wl_signal_add(&wlr_layer_shell->events.destroy, &manager->destroy);
    manager->new_surface.notify = handle_new_layer_surface;
    wl_signal_add(&wlr_layer_shell->events.new_surface, &manager->new_surface);

    manager->new_output.notify = handle_new_output;
    kywc_output_add_new_listener(&manager->new_output);

    return true;
}
