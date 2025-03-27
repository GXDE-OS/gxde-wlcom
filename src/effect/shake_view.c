// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later
#include <assert.h>
#include <stdlib.h>

#include <wlr/types/wlr_seat.h>

#include <kywc/log.h>

#include "effect/shake_view.h"
#include "effect_p.h"
#include "scene/animation.h"
#include "view/view.h"

/* An animation time */
#define SHAKE_EFFECT_PERIOD (40)
/* offset distance */
#define SHAKE_EFFECT_OFFSET (10)
/* Run times */
#define SHAKE_EFFECT_TIMES (3)

enum shake_effect_stage {
    SHAKE_EFFECT_ORIGIN = 0,
    SHAKE_EFFECT_LEFT_SIDE,
    SHAKE_EFFECT_RIGHT_SIDE,
};

struct shake_view {
    struct wl_list link;

    struct view *view;
    /* attributes used for the shake effect. */
    int current_stage;
    int count;
    bool enabled;
    /* view position and size before shake effect. */
    struct kywc_box geo;

    struct wl_listener view_unmap;
    struct wl_event_source *timer;
};

static struct shake_view_effect {
    struct wl_list shake_views;

    struct effect *effect;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;
} *effect = NULL;

static void shake_view_effect_enabled(struct shake_view *shake, bool enabled);

static int get_shake_view_effect_pending_stage(enum shake_effect_stage stage)
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

static void set_shake_view_effect_animation_time(struct shake_view *shake, int time)
{
    if (wl_event_source_timer_update(shake->timer, time) < 0) {
        kywc_log(KYWC_DEBUG, "failed to set key view shake animation timer");
    }
}

static void set_shake_view_effect_animation(struct shake_view *shake, enum shake_effect_stage stage,
                                            int time)
{
    int temp_x = shake->geo.x;
    switch (stage) {
    case SHAKE_EFFECT_ORIGIN:
        temp_x = shake->geo.x;
        break;
    case SHAKE_EFFECT_LEFT_SIDE:
        temp_x = shake->geo.x - SHAKE_EFFECT_OFFSET;
        break;
    case SHAKE_EFFECT_RIGHT_SIDE:
        temp_x = shake->geo.x + SHAKE_EFFECT_OFFSET;
        break;
    }

    struct animation *animation = animation_manager_get(ANIMATION_TYPE_EASE_IN_OUT);
    ky_scene_node_set_position_with_animation(&shake->view->tree->node, temp_x, shake->geo.y,
                                              animation, time);
}

/* current.x --> left.x --> right.x --> current.x
 * current.x --> left.x and right.x --> current.x same distance
 * left.x --> right.x is twice the distance
 */
static int handle_shake_view_effect(void *data)
{
    struct shake_view *shake = data;
    enum shake_effect_stage pending_stage =
        get_shake_view_effect_pending_stage(shake->current_stage);
    if (pending_stage == SHAKE_EFFECT_ORIGIN) {
        shake->count++;
    }

    set_shake_view_effect_animation_time(shake, pending_stage == SHAKE_EFFECT_RIGHT_SIDE
                                                    ? SHAKE_EFFECT_PERIOD * 2
                                                    : SHAKE_EFFECT_PERIOD);
    set_shake_view_effect_animation(
        shake, pending_stage,
        pending_stage == SHAKE_EFFECT_RIGHT_SIDE ? SHAKE_EFFECT_PERIOD * 2 : SHAKE_EFFECT_PERIOD);

    if (shake->count >= SHAKE_EFFECT_TIMES) {
        shake_view_effect_enabled(shake, false);
    }

    shake->current_stage = pending_stage;
    return 0;
}

static void shake_view_effect_enabled(struct shake_view *shake, bool enabled)
{
    if (!shake || shake->enabled == enabled) {
        return;
    }

    if (shake->enabled) {
        kywc_view_move(&shake->view->base, shake->geo.x, shake->geo.y);
    }

    shake->enabled = enabled;
    shake->count = 0;
    shake->geo = shake->view->base.geometry;
    shake->current_stage = SHAKE_EFFECT_ORIGIN;

    if (enabled) {
        handle_shake_view_effect(shake);
    } else if (wl_event_source_timer_update(shake->timer, 0) < 0) {
        kywc_log(KYWC_DEBUG, "failed to stop view shake timer");
    }
}

static struct shake_view *shake_view_by_view(struct view *view)
{
    struct shake_view *shake;
    wl_list_for_each(shake, &effect->shake_views, link) {
        if (shake->view == view) {
            return shake;
        }
    }

    return NULL;
}

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct shake_view *shake = wl_container_of(listener, shake, view_unmap);

    wl_list_remove(&shake->link);
    wl_list_remove(&shake->view_unmap.link);
    wl_event_source_remove(shake->timer);
    free(shake);
}

void view_add_shake_effect(struct view *view)
{
    if (!effect || !effect->effect->enabled) {
        return;
    }

    struct shake_view *shake = shake_view_by_view(view);
    if (shake) {
        shake_view_effect_enabled(shake, true);
        return;
    }

    shake = calloc(1, sizeof(struct shake_view));
    if (!shake) {
        return;
    }

    shake->view = view;
    wl_list_insert(&effect->shake_views, &shake->link);

    shake->view_unmap.notify = handle_view_unmap;
    wl_signal_add(&view->base.events.unmap, &shake->view_unmap);
    /* create timer for shake effect */
    struct seat *seat = input_manager_get_default_seat();
    struct wl_event_loop *loop = wl_display_get_event_loop(seat->wlr_seat->display);
    shake->timer = wl_event_loop_add_timer(loop, handle_shake_view_effect, shake);

    shake_view_effect_enabled(shake, true);
}

static void handle_effect_enable(struct wl_listener *listener, void *data)
{
    /* Do nothing, the effect is created by the module called independently. */
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    /* Do nothing, the effect is released by the module called independently. */
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    assert(wl_list_empty(&effect->shake_views));
    wl_list_remove(&effect->destroy.link);
    wl_list_remove(&effect->enable.link);
    wl_list_remove(&effect->disable.link);
    free(effect);
    effect = NULL;
}

static bool handle_effect_configure(struct effect *eff, const struct effect_option *option)
{
    return true; // Always on
}

static const struct effect_interface effect_impl = {
    .configure = handle_effect_configure,
};

bool shake_view_effect_create(struct effect_manager *effect_manager)
{
    effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }

    effect->effect = effect_create("shake_view", 0, true, &effect_impl, NULL);
    if (!effect->effect) {
        free(effect);
        effect = NULL;
        return false;
    }

    wl_list_init(&effect->shake_views);

    effect->enable.notify = handle_effect_enable;
    wl_signal_add(&effect->effect->events.enable, &effect->enable);
    effect->disable.notify = handle_effect_disable;
    wl_signal_add(&effect->effect->events.disable, &effect->disable);
    effect->destroy.notify = handle_effect_destroy;
    wl_signal_add(&effect->effect->events.destroy, &effect->destroy);

    return true;
}
