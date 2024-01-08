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

const char *scale_effect_name = "scale_effect";

struct scale_data {
    struct wl_listener minimize;
    struct wl_listener maximize;
    struct wl_listener show_desktop;
    bool is_show_desktop;
    struct wl_list item_transforms; // kywc_transform_item
};

struct kywc_transform_item {
    struct kywc_geometry_transform_node *node;
    struct wl_list link;

    struct wl_listener node_destroy;
};

static struct scale_data scale_data;

static void destroy_scale_effect(struct kywc_effect_view *view);

static void scale_effect_post(void)
{
    if (wl_list_empty(&scale_data.item_transforms)) {
        return;
    }
    struct kywc_transform_item *pos_item, *tmp_item;
    wl_list_for_each_safe(pos_item, tmp_item, &scale_data.item_transforms, link) {
        struct kywc_node *node = &pos_item->node->node.node;
        node->push_damage(node, NULL);
    }
}

static void transform_data_update(struct kywc_transform_data *data)
{
    struct kywc_effect_view *view = data->view;

    kywc_transform_data_calc_time_factor(data);

    data->animation_value = kywc_animation_value(data->animation, data->time_factor);

    kywc_transform_data_calc_alpha(data);

    kywc_transform_data_calc_view_box(data);

    kywc_transform_data_calc_padding_region(data, view);

    kywc_transform_data_update_location(data);
}

static void update_time_and_status(struct kywc_transform_data *data)
{
    if (data->time <= data->max_time) {
        data->view->effects_state = EFFECTS_ZOOMING;
    } else {
        data->view->effects_state = EFFECTS_END;
    }
}

static void scale_effect_prehook(void)
{
    if (wl_list_empty(&scale_data.item_transforms)) {
        return;
    }
    int current_time = kywc_get_current_time_msec();
    struct kywc_transform_item *pos_item, *tmp_item;
    wl_list_for_each_safe(pos_item, tmp_item, &scale_data.item_transforms, link) {
        struct kywc_transform_data *data = &pos_item->node->data;
        data->time = current_time;
        update_time_and_status(data);
        if (data->view->effects_state == EFFECTS_END) {
            destroy_scale_effect(data->view);
            continue;
        }

        kywc_effect_view_calc_shadow(data->view);
        transform_data_update(data);

        pos_item->node->node.node.push_damage(&pos_item->node->node.node, NULL);
    }
}

static void transform_item_release(struct kywc_transform_item *item)
{
    wl_list_remove(&item->node_destroy.link);
    wl_list_remove(&item->link);
    free(item);
}

static void item_handle_node_destroy(struct wl_listener *listener, void *data)
{
    struct kywc_transform_item *item = wl_container_of(listener, item, node_destroy);

    transform_item_release(item);
}

static void transform_item_init(struct kywc_transform_item *transform_item,
                                struct kywc_geometry_transform_node *node)
{
    transform_item->node = node;
    wl_list_init(&transform_item->link);
}

static struct kywc_transform_item *transform_item_create(struct kywc_geometry_transform_node *node)
{
    struct kywc_transform_item *transform_item = malloc(sizeof(struct kywc_transform_item));
    if (!transform_item) {
        return NULL;
    }
    transform_item_init(transform_item, node);

    transform_item->node_destroy.notify = item_handle_node_destroy;
    wl_signal_add(&node->node.node.events.destroy, &transform_item->node_destroy);
    return transform_item;
}

static void destroy_scale_effect(struct kywc_effect_view *view)
{
    if (!view || !view->view_node) {
        return;
    }
    scale_data.is_show_desktop = false;
    struct kywc_group_node *group_node =
        kywc_node_transform_remove(&view->view_node->node, scale_effect_name);
    if (!group_node) {
        return;
    }

    struct kywc_geometry_transform_node *geo_node = wl_container_of(group_node, geo_node, node);

    kywc_animation_destroy(geo_node->data.animation);
    kywc_node_destroy(&group_node->node);

    if (kywc_effect_view_is_minimized(view)) {
        view->impl->effect_set_view_visible(view, false);
    }
}

static struct kywc_geometry_transform_node *create_scale_effect(struct kywc_effect_view *view)
{
    if (!view) {
        return NULL;
    }
    struct kywc_group_node *transform_node =
        kywc_node_transform_get(&view->view_node->node, scale_effect_name);
    if (!transform_node) {
        struct kywc_geometry_transform_node *node = kywc_transform_geometry_node_create(view);
        struct kywc_transform_item *transform_item = transform_item_create(node);
        if (!transform_item) {
            kywc_node_destroy(&node->node.node);
            return NULL;
        }

        wl_list_insert(&scale_data.item_transforms, &transform_item->link);
        bool ret =
            kywc_node_transform_add(&view->view_node->node, &node->node, 1, scale_effect_name);
        if (!ret) {
            transform_item_release(transform_item);
            kywc_node_destroy(&node->node.node);
            return NULL;
        }
        return node;
    }

    struct kywc_geometry_transform_node *node = wl_container_of(transform_node, node, node);
    /* After exceeding this value, compressing a small value between 0 and 1 may cause jitter */
    if (node->data.time_factor < 0.95) {
        if (node->data.max_time - node->data.time < 0.4 * TRANSFORM_TIME) {
            node->data.max_time = 0.5 * TRANSFORM_TIME + node->data.time;
        }
    }
    return node;
}

static void get_view_box(struct kywc_effect_view *view, struct kywc_box *in_box,
                         struct kywc_box *out_box)
{
    struct kywc_view *_view = view->kywc_view;
    out_box->x = in_box->x - _view->margin.off_x;
    out_box->y = in_box->y - _view->margin.off_y;
    out_box->width = in_box->width + _view->margin.off_width;
    out_box->height = in_box->height + _view->margin.off_height;
}

static void get_geometry_box(struct kywc_effect_view *view, struct kywc_box *box)
{
    struct kywc_view *_view = view->kywc_view;
    box->x = _view->geometry.x;
    box->y = _view->geometry.y;
    box->width = _view->geometry.width;
    box->height = _view->geometry.height;
}

static void maximized_alpha_func_init(struct kywc_transform_data *data)
{
    if (!data) {
        return;
    }
    // start maximize
    if (data->time == data->start_time) {
        data->alpha = 1;
        kywc_transform_data_alpha_func_init(data, 1);
    }
}

static void maximize_update_view_box(struct kywc_effect_view *view)
{
    if (!view) {
        return;
    }
    struct kywc_view *kywc_view = view->kywc_view;
    struct kywc_box save_geometry_box = { 0 };
    struct kywc_box geometry_box = { 0 };

    get_geometry_box(view, &geometry_box);
    kywc_effect_view_get_save_geometry(view, &save_geometry_box);
    if (kywc_view->maximized) {
        get_view_box(view, &save_geometry_box, &view->last_box);
    } else {
        view->last_box = view->dst_box;
    }
    get_view_box(view, &geometry_box, &view->dst_box);
}

static void handle_view_maximize(struct wl_listener *listener, void *data)
{
    struct kywc_effect_view *view = data;

    struct kywc_geometry_transform_node *node = create_scale_effect(view);
    maximize_update_view_box(view);
    kywc_transform_data_time_func_init(&node->data);
    maximized_alpha_func_init(&node->data);
    kywc_transform_data_view_box_func_init(&node->data);
}

static void minimized_alpha_func_init(struct kywc_transform_data *data)
{
    if (!data) {
        return;
    }
    if (data->time == data->start_time) {
        // start
        if (data->view->kywc_view->minimized) {
            // minimized
            data->time_factor = 0;
            data->alpha = 1;
        } else {
            // minimized restore
            data->time_factor = 0;
            data->alpha = 0;
        }
    }

    if (data->view->kywc_view->minimized) {
        kywc_transform_data_alpha_func_init(data, 0);
    } else {
        kywc_transform_data_alpha_func_init(data, 1);
    }
}

static void minimize_update_view_box(struct kywc_effect_view *view)
{
    if (!view) {
        return;
    }
    struct kywc_view *kywc_view = view->kywc_view;
    struct kywc_box save_geometry_box = { 0 };
    struct kywc_box geometry_box = { 0 };
    geometry_box.height = 10;
    geometry_box.width = 10;
    if (scale_data.is_show_desktop) {
        kywc_effect_view_get_end_box(view, OUTPUT_CENTER, &geometry_box);
    } else {
        kywc_effect_view_get_end_box(view, VIEW_CENTER, &geometry_box);
    }
    get_geometry_box(view, &save_geometry_box);
    if (kywc_view->minimized) {
        get_view_box(view, &save_geometry_box, &view->last_box);
        get_view_box(view, &geometry_box, &view->dst_box);
    } else {
        if (scale_data.is_show_desktop) {
            get_view_box(view, &geometry_box, &view->last_box);
        } else {
            get_view_box(view, &view->dst_box, &view->last_box);
        }
        get_view_box(view, &save_geometry_box, &view->dst_box);
    }
}

static void handle_view_minimize(struct wl_listener *listener, void *data)
{
    struct kywc_effect_view *view = data;
    if (kywc_effect_view_is_minimized(view)) {
        view->impl->effect_set_view_visible(view, true);
        kywc_node_raise_to_top(&view->view_node->node);
    }
    struct kywc_geometry_transform_node *node = create_scale_effect(view);
    minimize_update_view_box(view);
    kywc_transform_data_time_func_init(&node->data);
    minimized_alpha_func_init(&node->data);
    kywc_transform_data_view_box_func_init(&node->data);
}

static void handle_view_show_desktop(struct wl_listener *listener, void *data)
{
    if (wl_list_empty(&scale_data.item_transforms)) {
        return;
    }
    scale_data.is_show_desktop = true;
    struct kywc_transform_item *pos_item, *tmp_item;
    wl_list_for_each_safe(pos_item, tmp_item, &scale_data.item_transforms, link) {
        struct kywc_transform_data *data = &pos_item->node->data;
        minimize_update_view_box(data->view);
        kywc_transform_data_time_func_init(data);
        minimized_alpha_func_init(data);
        kywc_transform_data_view_box_func_init(data);
    }
}

static bool scale_plugin_init(void *plugin, void **teardown_data)
{
    struct kywc_effect_server *s = kywc_effect_server();
    if (!s || !kywc_renderer_is_opengl(s)) {
        kywc_log(KYWC_ERROR, "Scale_plugin_init failed");
        return false;
    }

    kywc_effect_add_hook(OUTPUT_EFFECT_PRE, &scale_effect_prehook);
    kywc_effect_add_hook(OUTPUT_EFFECT_POST, &scale_effect_post);

    wl_list_init(&scale_data.item_transforms);

    scale_data.minimize.notify = handle_view_minimize;
    wl_signal_add(&s->events.view_minimize, &scale_data.minimize);

    scale_data.maximize.notify = handle_view_maximize;
    wl_signal_add(&s->events.view_maximize, &scale_data.maximize);

    scale_data.show_desktop.notify = handle_view_show_desktop;
    kywc_view_add_show_desktop_listener(&scale_data.show_desktop);

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
