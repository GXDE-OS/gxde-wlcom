// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_seat.h>

#include "input/event.h"
#include "scene/animation.h"
#include "scene/surface.h"
#include "theme.h"
#include "view_p.h"

enum shake_effect_stage {
    SHAKE_EFFECT_ORIGIN = 0,
    SHAKE_EFFECT_LEFT_SIDE,
    SHAKE_EFFECT_RIGHT_SIDE,
};

struct modal {
    struct view *view;
    struct wl_listener view_unmap;
    struct wl_listener view_minimized;
    struct wl_listener view_position;
    struct wl_listener view_activate;

    /* attributes used for the shake effect. */
    struct {
        int current_stage;
        int period;
        int offset;
        int times;
        int count;
        bool enabled;
    } shake_effect;

    /* view position and size before shake effect. */
    struct kywc_box geo;
    /* records the resizeble maximizable fullscreenable flag of the superview. */
    bool parent_resizable, parent_maximizable, parent_fullscreenable;

    struct ky_scene_rect *modal_box;
    struct wl_event_source *timer;
};

static void modal_shake_effect_set_enabled(struct modal *modal, bool enabled)
{
    if (modal->shake_effect.enabled == enabled) {
        return;
    }

    modal->shake_effect.enabled = enabled;

    if (wl_event_source_timer_update(modal->timer, enabled ? 1 : 0) < 0) {
        kywc_log(KYWC_DEBUG, "failed to set key modal timer");
    }
}

static void set_shake_effect_animation_time(struct modal *modal, int time)
{
    if (wl_event_source_timer_update(modal->timer, time) < 0) {
        kywc_log(KYWC_DEBUG, "failed to set key modal animation timer");
    }
}

static int get_shake_effect_pending_stage(enum shake_effect_stage stage)
{
    enum shake_effect_stage pending_stage = SHAKE_EFFECT_ORIGIN;
    switch (stage) {
    case SHAKE_EFFECT_ORIGIN:
        pending_stage = SHAKE_EFFECT_LEFT_SIDE;
        break;
    case SHAKE_EFFECT_LEFT_SIDE:
        pending_stage = SHAKE_EFFECT_RIGHT_SIDE;
        break;
    case SHAKE_EFFECT_RIGHT_SIDE:
        pending_stage = SHAKE_EFFECT_ORIGIN;
        break;
    }
    return pending_stage;
}

static void set_shake_effect_animation(struct modal *modal, enum shake_effect_stage stage, int time)
{
    int temp_x = modal->geo.x;
    switch (stage) {
    case SHAKE_EFFECT_ORIGIN:
        temp_x = modal->geo.x;
        break;
    case SHAKE_EFFECT_LEFT_SIDE:
        temp_x = modal->geo.x - modal->shake_effect.offset;
        break;
    case SHAKE_EFFECT_RIGHT_SIDE:
        temp_x = modal->geo.x + modal->shake_effect.offset;
        break;
    }

    struct animation *animation = animation_manager_get_default(ANIMATION_TYPE_EASE_IN_OUT);
    ky_scene_node_set_position_with_animation(&modal->view->tree->node, temp_x, modal->geo.y,
                                              animation, time);
}

/* current.x --> left.x --> right.x --> current.x
 * current.x --> left.x and right.x --> current.x same distance
 * left.x --> right.x is twice the distance
 */
static int handle_modal_shake_effect(void *data)
{
    struct modal *modal = data;
    enum shake_effect_stage pending_stage =
        get_shake_effect_pending_stage(modal->shake_effect.current_stage);
    if (pending_stage == SHAKE_EFFECT_ORIGIN) {
        modal->shake_effect.count++;
    }

    set_shake_effect_animation_time(modal, pending_stage == SHAKE_EFFECT_RIGHT_SIDE
                                               ? modal->shake_effect.period * 2
                                               : modal->shake_effect.period);
    set_shake_effect_animation(modal, pending_stage,
                               pending_stage == SHAKE_EFFECT_RIGHT_SIDE
                                   ? modal->shake_effect.period * 2
                                   : modal->shake_effect.period);

    if (modal->shake_effect.count >= modal->shake_effect.times) {
        modal_shake_effect_set_enabled(modal, false);
        modal->shake_effect.count = 0;
    }

    modal->shake_effect.current_stage = pending_stage;
    return 0;
}

static bool modal_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                        uint32_t time, bool first, bool hold, void *data)
{
    return false;
}

static void modal_leave(struct seat *seat, struct ky_scene_node *node, bool last, void *data) {}

static void modal_shake_effect_init(struct modal *modal)
{
    if (modal->shake_effect.enabled) {
        kywc_view_move(&modal->view->base, modal->geo.x, modal->geo.y);
    }

    modal->geo = modal->view->base.geometry;
    modal->shake_effect.count = 0;
    modal->shake_effect.current_stage = SHAKE_EFFECT_ORIGIN;

    /* active current view */
    kywc_view_activate(&modal->view->base);
    modal_shake_effect_set_enabled(modal, true);
}

static void modal_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                        bool pressed, uint32_t time, enum click_state state, void *data)
{
    if (!pressed) {
        return;
    }

    struct modal *modal = data;
    modal_shake_effect_init(modal);
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

static void modal_destroy(struct modal *modal)
{
    if (modal->view->parent) {
        modal->view->parent->base.resizable = modal->parent_resizable;
        modal->view->parent->base.maximizable = modal->parent_maximizable;
        modal->view->parent->base.fullscreenable = modal->parent_fullscreenable;
    }

    wl_list_remove(&modal->view_unmap.link);
    wl_list_remove(&modal->view_minimized.link);
    wl_list_remove(&modal->view_position.link);
    wl_list_remove(&modal->view_activate.link);
    wl_event_source_remove(modal->timer);
    ky_scene_node_destroy(&modal->modal_box->node);
    free(modal);
}

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct modal *modal = wl_container_of(listener, modal, view_unmap);
    modal_destroy(modal);
}

static void handle_view_minimized(struct wl_listener *listener, void *data)
{
    struct modal *modal = wl_container_of(listener, modal, view_minimized);
    ky_scene_node_set_enabled(&modal->modal_box->node, !modal->view->parent->base.minimized);
}

static void handle_view_position(struct wl_listener *listener, void *data)
{
    struct modal *modal = wl_container_of(listener, modal, view_position);
    struct kywc_view *kywc_view = &modal->view->parent->base;
    int x = kywc_view->geometry.x - kywc_view->margin.off_x;
    int y = kywc_view->geometry.y - kywc_view->margin.off_y;
    ky_scene_node_set_position(&modal->modal_box->node, x, y);
}

static void handle_view_activate(struct wl_listener *listener, void *data)
{
    struct modal *modal = wl_container_of(listener, modal, view_activate);
    modal_shake_effect_init(modal);
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
        bool need_corner = !kywc_view->maximized && !kywc_view->fullscreen;
        bool need_right_top = need_corner && (kywc_view->tiled == KYWC_TILE_NONE ||
                                              kywc_view->tiled == KYWC_TILE_BOTTOM_LEFT);
        bool need_left_top = need_corner && (kywc_view->tiled == KYWC_TILE_NONE ||
                                             kywc_view->tiled == KYWC_TILE_BOTTOM_RIGHT);

        struct theme *theme = theme_manager_get_current();
        radius[KY_SCENE_ROUND_CORNER_RT] = need_right_top ? theme->ssd.corner_radius : 0;
        radius[KY_SCENE_ROUND_CORNER_LT] = need_left_top ? theme->ssd.corner_radius : 0;
    }

    ky_scene_node_set_radius(&modal_box->node, radius);
}

void modal_create(struct view *view, struct seat *seat)
{
    if (!view->base.modal || !view->parent || !view->parent->surface) {
        return;
    }

    struct modal *modal = calloc(1, sizeof(struct modal));
    if (!modal) {
        return;
    }

    modal->view = view;
    modal->parent_resizable = view->parent->base.resizable;
    modal->parent_maximizable = view->parent->base.maximizable;
    modal->parent_fullscreenable = view->parent->base.fullscreenable;
    view->parent->base.resizable = false;
    view->parent->base.maximizable = false;
    view->parent->base.fullscreenable = false;

    struct kywc_view *kywc_view = &view->parent->base;
    struct kywc_box geo = {
        .x = kywc_view->geometry.x - kywc_view->margin.off_x,
        .y = kywc_view->geometry.y - kywc_view->margin.off_y,
        .width = kywc_view->geometry.width + kywc_view->margin.off_width,
        .height = kywc_view->geometry.height + kywc_view->margin.off_height,
    };

    struct theme *theme = theme_manager_get_current();
    /* An animation time */
    modal->shake_effect.period = theme->modal.shake_effect_period;
    /* offset distance */
    modal->shake_effect.offset = theme->modal.shake_effect_offset;
    /* Run times */
    modal->shake_effect.times = theme->modal.shake_effect_times;

    modal->modal_box =
        ky_scene_rect_create(view->tree->node.parent, geo.width, geo.height, theme->modal.color);
    ky_scene_node_lower_to_bottom(&modal->modal_box->node);
    ky_scene_node_set_position(&modal->modal_box->node, geo.x, geo.y);
    modal_box_set_round_corner(modal->modal_box, view->parent);
    ky_scene_node_set_enabled(&modal->modal_box->node, true);

    input_event_node_create(&modal->modal_box->node, &modal_impl, modal_get_root, NULL, modal);

    modal->view_unmap.notify = handle_view_unmap;
    wl_signal_add(&view->base.events.unmap, &modal->view_unmap);
    /* The gray area is hidden when the desktop is displayed */
    modal->view_minimized.notify = handle_view_minimized;
    wl_signal_add(&kywc_view->events.minimize, &modal->view_minimized);
    /* When the parent view is move for reasons such as protocol */
    modal->view_position.notify = handle_view_position;
    wl_signal_add(&kywc_view->events.position, &modal->view_position);
    /* When the parent view is activate for reasons such as protocol */
    modal->view_activate.notify = handle_view_activate;
    wl_signal_add(&kywc_view->events.activate, &modal->view_activate);

    /* create timer for modal shake effect*/
    struct wl_event_loop *loop = wl_display_get_event_loop(seat->wlr_seat->display);
    modal->timer = wl_event_loop_add_timer(loop, handle_modal_shake_effect, modal);
}
