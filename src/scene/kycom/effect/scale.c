// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#include "kywc/kycom/effect_transform.h"
#include "kywc/kycom/opengl.h"
#include "kywc/kycom/scene.h"
#include "kywc/kycom/transform.h"
#include "kywc/log.h"
#include "kywc/plugin.h"
#include "scene/kycom/effect_view_impl.h"

enum ky_scale_state {
    PRE_STATE = 0,
    ZOOM_STATE,
    END_STATE,
};

struct scale_data {
    struct wl_listener minimize;
    struct wl_listener maximize;
    enum ky_scale_state state;
};

static int32_t end_time;
static int32_t start_time;
static struct scale_data scale_data;

static struct kywc_transform_geometry_node *current_transform_node;

static void scale_effect_post(void)
{
    struct wlr_box local_damage = {
        .x = kywc_transform_data.geometry_box.x - kywc_transform_data.padding.left,
        .y = kywc_transform_data.geometry_box.y - kywc_transform_data.padding.right,
        .width = kywc_transform_data.geometry_box.width + kywc_transform_data.padding.top +
                 kywc_transform_data.padding.left,
        .height = kywc_transform_data.geometry_box.height + kywc_transform_data.padding.bottom +
                  kywc_transform_data.padding.right,
    };

    struct kywc_node *node = &current_transform_node->node.node;
    node->push_damage(node, &local_damage);
}

static void scale_effect_run(void)
{
    switch (scale_data.state) {
    case PRE_STATE:
        if (kywc_effect_view_is_minimized(kywc_transform_data.view)) {
            kywc_node_raise_to_top(&kywc_transform_data.view->view_node->node);
        }
        scale_data.state = ZOOM_STATE;
        break;
    case ZOOM_STATE:
        end_time = kywc_current_time_msec();
        if (end_time - start_time < TRANSFORM_TIME) {
            scale_data.state = ZOOM_STATE;
            kywc_transform_data.view->effects_state = EFFECTS_ZOOMING;
        } else {
            scale_data.state = END_STATE;
        }
        break;
    case END_STATE:
        kywc_transform_data.view->effects_state = EFFECTS_END;
        break;
    default:
        scale_data.state = END_STATE;
        kywc_transform_data.view->effects_state = EFFECTS_END;
        break;
    }
}

static void scale_effect_prehook(void)
{
    scale_effect_run();
    if (scale_data.state == END_STATE && kywc_transform_data.view->effects_state == EFFECTS_END) {
        if (kywc_effect_view_is_minimized(kywc_transform_data.view)) {
            kywc_transform_data.view->impl->effect_set_view_visible(kywc_transform_data.view,
                                                                    false);
        }
        kywc_node_transform_remove(&kywc_transform_data.view_node->node, kywc_transform_name);
        kywc_effect_remove_hook(&scale_effect_prehook);
        kywc_effect_remove_hook(&scale_effect_post);
        kywc_transform_data.view = NULL;
        kywc_transform_geometry_node_destroy(current_transform_node);
        return;
    }

    kywc_transform_data.time_factor = kywc_current_time_factor(start_time, end_time);

    kywc_geometry_current_box(&kywc_transform_data);

    struct kywc_effect_view *view = kywc_transform_data.view;
    struct kywc_effect_box bound_box = { 0 };
    struct wlr_box local_damage = { 0 };
    struct padding padding_tmp = { 0 };
    kywc_get_bound_box(view, &bound_box);
    kywc_effect_view_get_shadow_box(&bound_box, view);

    kywc_effect_view_get_padding_region(kywc_transform_data.view, &kywc_transform_data.geometry_box,
                                        &padding_tmp);
    memcpy(&kywc_transform_data.padding, &padding_tmp, sizeof(padding_tmp));

    kywc_transform_local_damage(&local_damage, &kywc_transform_data);

    current_transform_node->node.node.push_damage(&current_transform_node->node.node,
                                                  &local_damage);
}

static void handle_add_effect(struct kywc_effect_view *view)
{
    if (!view) {
        return;
    }
    scale_data.state = PRE_STATE;
    start_time = kywc_current_time_msec();
    kywc_effect_add_hook(OUTPUT_EFFECT_PRE, &scale_effect_prehook);
    kywc_effect_add_hook(OUTPUT_EFFECT_POST, &scale_effect_post);

    struct kywc_transform_geometry_node *transform_node =
        kywc_transform_geometry_node_create(&kywc_transform_data);
    current_transform_node = transform_node;

    bool ret = kywc_node_transform_add(&kywc_transform_data.view_node->node, &transform_node->node,
                                       1, kywc_transform_name);
    if (!ret) {
        kywc_transform_geometry_node_destroy(transform_node);
    }
}

static void scale_update_ssd_box(struct kywc_effect_box *in_box, struct kywc_effect_box *out_box,
                                 struct kywc_effect_view *view)
{
    struct kywc_view *_view = view->kywc_view;
    out_box->x = in_box->x - _view->margin.off_x;
    out_box->y = in_box->y - _view->margin.off_y;
    out_box->width = in_box->width + _view->margin.off_width;
    out_box->height = in_box->height + _view->margin.off_height;
}

static void scale_get_view_geometry_box(struct kywc_effect_view *view, struct kywc_effect_box *box)
{
    struct kywc_view *_view = view->kywc_view;
    box->x = _view->geometry.x;
    box->y = _view->geometry.y;
    box->width = _view->geometry.width;
    box->height = _view->geometry.height;
}

static void maximize_update_box(struct kywc_effect_view *view)
{
    struct kywc_view *kywc_view = view->kywc_view;
    struct kywc_effect_box last_box = { 0 };
    struct kywc_effect_box dst_box = { 0 };

    scale_get_view_geometry_box(view, &dst_box);
    kywc_effect_view_get_geometry_box(view, &last_box);
    if (kywc_view->ssd == KYWC_SSD_ALL) {
        if (kywc_view->maximized) {
            scale_update_ssd_box(&dst_box, &view->dst_box, view);
            scale_update_ssd_box(&last_box, &view->last_box, view);
        } else {
            struct kywc_effect_box last_box = {
                .x = view->dst_box.x,
                .y = view->dst_box.y,
                .height = view->dst_box.height,
                .width = view->dst_box.width,
            };
            memcpy(&kywc_transform_data.view->last_box, &last_box, sizeof(last_box));
            scale_update_ssd_box(&dst_box, &view->dst_box, view);
        }
    } else {
        if (kywc_view->maximized) {
            memcpy(&kywc_transform_data.view->last_box, &last_box, sizeof(last_box));
            memcpy(&kywc_transform_data.view->dst_box, &dst_box, sizeof(dst_box));
        } else {
            struct kywc_effect_box last_box = {
                .x = view->dst_box.x,
                .y = view->dst_box.y,
                .height = view->dst_box.height,
                .width = view->dst_box.width,
            };
            memcpy(&kywc_transform_data.view->last_box, &last_box, sizeof(last_box));
            memcpy(&kywc_transform_data.view->dst_box, &dst_box, sizeof(dst_box));
        }
    }
}

static void handle_view_maximize(struct wl_listener *listener, void *data)
{
    struct kywc_effect_view *view = data;
    if (!view || scale_data.state != END_STATE || view->effects_state != EFFECTS_END) {
        return;
    }
    kywc_transform_data.view = data;
    kywc_transform_data.view->impl = view->impl;
    kywc_transform_data.view_node = view->view_node;
    handle_add_effect(kywc_transform_data.view);
    maximize_update_box(kywc_transform_data.view);
}

static void minimize_update_box(struct kywc_effect_view *view)
{
    if (!view) {
        return;
    }
    struct kywc_view *kywc_view = view->kywc_view;
    struct kywc_effect_box last_box = { 0 };
    struct kywc_effect_box dst_box = { 0 };
    dst_box.x = (kywc_view->geometry.x + kywc_view->geometry.width) / 2;
    dst_box.y = (kywc_view->geometry.y + kywc_view->geometry.height) / 2;
    dst_box.height = 10;
    dst_box.width = 10;
    scale_get_view_geometry_box(view, &last_box);
    if (kywc_view->ssd == KYWC_SSD_ALL) {
        if (kywc_view->minimized) {
            scale_update_ssd_box(&dst_box, &kywc_transform_data.view->dst_box, view);
            scale_update_ssd_box(&last_box, &kywc_transform_data.view->last_box, view);
        } else {
            scale_get_view_geometry_box(view, &dst_box);
            memcpy(&kywc_transform_data.view->last_box, &kywc_transform_data.view->dst_box,
                   sizeof(kywc_transform_data.view->dst_box));
            memcpy(&kywc_transform_data.view->dst_box, &dst_box, sizeof(dst_box));
            scale_update_ssd_box(&kywc_transform_data.view->last_box,
                                 &kywc_transform_data.view->last_box, view);
            scale_update_ssd_box(&dst_box, &kywc_transform_data.view->dst_box, view);
        }
    } else {
        if (kywc_view->minimized) {
            memcpy(&kywc_transform_data.view->last_box, &last_box, sizeof(last_box));
            memcpy(&kywc_transform_data.view->dst_box, &dst_box, sizeof(dst_box));
        } else {
            memcpy(&kywc_transform_data.view->dst_box, &last_box, sizeof(dst_box));
            memcpy(&kywc_transform_data.view->last_box, &dst_box, sizeof(last_box));
        }
    }
}

static void handle_view_minimize(struct wl_listener *listener, void *data)
{
    struct kywc_effect_view *view = data;
    if (!view || scale_data.state != END_STATE || view->effects_state != EFFECTS_END) {
        return;
    }
    kywc_transform_data.view = data;
    kywc_transform_data.view->impl = view->impl;
    kywc_transform_data.view_node = view->view_node;

    kywc_transform_data.view->impl->effect_set_view_visible(kywc_transform_data.view, true);
    handle_add_effect(kywc_transform_data.view);
    minimize_update_box(kywc_transform_data.view);
}

static bool scale_plugin_init(void *plugin, void **teardown_data)
{
    struct kywc_effect_server *s = kywc_effect_server();
    if (!s || !kywc_renderer_is_opengl(s)) {
        kywc_log(KYWC_ERROR, "Scale_plugin_init failed");
        return false;
    }
    scale_data.state = END_STATE;
    kywc_transform_data.view = NULL;

    scale_data.minimize.notify = handle_view_minimize;
    wl_signal_add(&s->events.view_minimize, &scale_data.minimize);

    scale_data.maximize.notify = handle_view_maximize;
    wl_signal_add(&s->events.view_maximize, &scale_data.maximize);

    return true;
}

static void scale_plugin_teardown(void *teardown_data)
{
    kywc_effect_remove_hook(&scale_effect_prehook);

    kywc_effect_remove_hook(&scale_effect_post);
}

static struct kywc_plugin_info info = {
    .name = "kywc_scale_effect.so",
    .abi_version = 1,
    .version = 1,
    .class = "scale",
};

struct kywc_plugin_data libkywc_scale_effect_plugin_data = {
    .info = &info,
    .options = NULL,
    .option = NULL,
    .setup = scale_plugin_init,
    .teardown = scale_plugin_teardown,
};
