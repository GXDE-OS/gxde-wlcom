// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>
#include <string.h>

#include "effect/scale.h"
#include "effect/transform.h"
#include "effect_p.h"
#include "kywc/boxes.h"
#include "output.h"
#include "scene/surface.h"
#include "theme.h"
#include "util/time.h"
#include "view/workspace.h"

struct padding {
    int top, bottom, left, right;
};

struct scale_entity {
    struct transform *transform;
    struct view *view;
    struct padding shadow;

    struct wl_listener view_destroy;
    struct wl_listener destroy;
};

struct scale_effect {
    struct transform_effect *effect;
};

static struct scale_effect *scale_effect = NULL;

static void scale_calc_view_box(struct kywc_view *view, struct kywc_box *geometry_box,
                                struct kywc_box *box)
{
    box->x = geometry_box->x - view->margin.off_x;
    box->y = geometry_box->y - view->margin.off_y;
    box->width = geometry_box->width + view->margin.off_width;
    box->height = geometry_box->height + view->margin.off_height;
}

static void scale_get_node_geometry(struct ky_scene_node *node, struct kywc_box *geometry)
{
    struct wlr_box box;
    node->impl.get_bounding_box(node, &box);
    ky_scene_node_coords(node, &geometry->x, &geometry->y);
    geometry->x += box.x;
    geometry->y += box.y;
    geometry->width = box.width;
    geometry->height = box.height;
}

static void scale_calc_minimize_start_and_end_geometry(struct view *view, struct kywc_box *start,
                                                       struct kywc_box *end)
{
    struct kywc_box end_box = { 0, 0, 10, 10 };
    if (view->minimized_when_show_desktop) {
        struct output *output = output_from_kywc_output(view->output);
        end_box.x = output->geometry.x + (output->geometry.width - end_box.width) / 2;
        end_box.y = output->geometry.y + (output->geometry.height - end_box.height) / 2;
    } else {
        if (view->minimized_geometry.panel_surface) {
            int lx, ly;
            struct ky_scene_buffer *buffer =
                ky_scene_buffer_try_from_surface(view->minimized_geometry.panel_surface);
            ky_scene_node_coords(&buffer->node, &lx, &ly);

            end_box.x = view->minimized_geometry.x + lx;
            end_box.y = view->minimized_geometry.y + ly;
            end_box.width = view->minimized_geometry.width;
            end_box.height = view->minimized_geometry.height;
        } else {
            end_box.x = view->base.geometry.x + view->base.geometry.width / 2;
            end_box.y = view->base.geometry.y + view->base.geometry.height / 2;
        }
    }

    struct kywc_box start_geometry = view->base.minimized ? view->base.geometry : end_box;
    scale_calc_view_box(&view->base, &start_geometry, start);
    struct kywc_box end_geometry = view->base.minimized ? end_box : view->base.geometry;
    scale_calc_view_box(&view->base, &end_geometry, end);
}

static void scale_handle_transform_destroy(struct wl_listener *listener, void *data)
{
    struct scale_entity *scale_entity = wl_container_of(listener, scale_entity, destroy);
    struct view *view = scale_entity->view;
    if (view->base.minimized) {
        view->current_proxy ? ky_scene_node_lower_to_bottom(&view->current_proxy->tree->node)
                            : ky_scene_node_lower_to_bottom(&view->tree->node);
    }
    wl_list_remove(&scale_entity->view_destroy.link);
    wl_list_remove(&scale_entity->destroy.link);
    free(scale_entity);
}

static void scale_handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct scale_entity *scale_entity = wl_container_of(listener, scale_entity, view_destroy);
    transform_destroy(scale_entity->transform);
}

static void scale_update_transform_options(struct transform_effect *effect,
                                           struct transform *transform,
                                           struct transform_options *options,
                                           struct animation_data *current, bool interrupted,
                                           void *data)
{
    struct scale_entity *scale_entity = transform_get_user_data(transform);
    struct view *view = scale_entity->view;
    if (view->base.minimized && !interrupted) {
        return;
    }

    if (interrupted) {
        options->start_time = current_time_msec();
    }

    struct kywc_box node_geometry;
    scale_get_node_geometry(&view->tree->node, &node_geometry);
    /* not update when minimized or geometry is equal */
    if (view->base.minimized || kywc_box_equal(&node_geometry, &options->end.geometry)) {
        return;
    }

    scale_calc_view_box(&view->base, &view->base.geometry, &options->end.geometry);
}

static void scale_get_render_dst_box(struct transform_effect *effect, struct transform *transform,
                                     struct kywc_box *dst_box, void *data)
{
    struct scale_entity *scale_entity = transform_get_user_data(transform);
    struct kywc_box node_geometry;
    scale_get_node_geometry(&scale_entity->view->tree->node, &node_geometry);
    int width = node_geometry.width - scale_entity->shadow.right - scale_entity->shadow.left;
    int height = node_geometry.height - scale_entity->shadow.top - scale_entity->shadow.bottom;
    float width_scale = 1.0 * dst_box->width / width;
    float height_scale = 1.0 * dst_box->height / height;

    struct padding shadow = {
        .left = width_scale * scale_entity->shadow.left,
        .right = width_scale * scale_entity->shadow.right,
        .top = ceil(height_scale * scale_entity->shadow.top),
        .bottom = ceil(height_scale * scale_entity->shadow.bottom),
    };

    dst_box->x -= shadow.left;
    dst_box->y -= shadow.top;
    dst_box->width += shadow.right + shadow.left;
    dst_box->height += shadow.bottom + shadow.top;
}

static void scale_effect_destroy(struct transform_effect *effect, void *data)
{
    struct scale_effect *scale_effect = data;
    free(scale_effect);
    scale_effect = NULL;
}

struct transform_effect_interface scale_impl = {
    .update_transform_options = scale_update_transform_options,
    .get_render_dst_box = scale_get_render_dst_box,
    .destroy = scale_effect_destroy,
};

bool scale_effect_create(struct effect_manager *manager)
{
    scale_effect = calloc(1, sizeof(*scale_effect));
    if (!scale_effect) {
        return false;
    }

    scale_effect->effect = transform_effect_create(manager, &scale_impl, "scale", 5, scale_effect);
    if (!scale_effect->effect) {
        free(scale_effect);
        return false;
    }

    return true;
}

bool view_add_scale_effect(struct view *view, enum scale_action action)
{
    if (!scale_effect) {
        return false;
    }

    /* do not display maximize effect when view minimized */
    if (view->base.minimized && action == SCALE_MAXIMIZE) {
        return false;
    }

    struct scale_entity *scale_entity = calloc(1, sizeof(*scale_entity));
    if (!scale_entity) {
        return false;
    }

    scale_entity->view = view;
    struct theme *theme = theme_manager_get_current();
    if (view->base.ssd == KYWC_SSD_NONE) {
        memcpy(&scale_entity->shadow, &view->base.padding, sizeof(struct padding));
    } else {
        scale_entity->shadow.top = scale_entity->shadow.bottom = theme->shadow_border;
        scale_entity->shadow.left = scale_entity->shadow.right = theme->shadow_border;
    }

    struct transform_options options = { 0 };
    options.start_time = current_time_msec();
    if (action == SCALE_MAXIMIZE) {
        options.start.alpha = 1.0;
        options.end.alpha = 1.0;
        options.duration = view->base.maximized ? 300 : 260;
        options.type.geometry = ANIMATION_TYPE_EASE;
        scale_calc_view_box(&view->base, &view->pending.geometry, &options.start.geometry);
        scale_calc_view_box(&view->base, &view->base.geometry, &options.end.geometry);
    } else if (action == SCALE_MINIMIZE) {
        if (view->base.minimized) {
            options.duration = 260;
            options.start.alpha = 1.0;
            options.end.alpha = 0;
            options.type.geometry = ANIMATION_TYPE_0_40_20_100;
            options.type.alpha = ANIMATION_TYPE_33_0_100_75;
            ky_scene_node_raise_to_top(view->current_proxy ? &view->current_proxy->tree->node
                                                           : &view->tree->node);
        } else {
            options.start.alpha = 0;
            options.end.alpha = 1.0;
            options.duration = 300;
            options.type.geometry = ANIMATION_TYPE_30_15_10_100;
            options.type.alpha = ANIMATION_TYPE_0_40_20_100;
        }
        scale_calc_minimize_start_and_end_geometry(view, &options.start.geometry,
                                                   &options.end.geometry);
    }

    options.buffer = transform_get_zero_copy_buffer(view);

    scale_entity->transform = transform_effect_get_or_create_transform(
        scale_effect->effect, &options, &view->tree->node, scale_entity);
    if (!scale_entity->transform) {
        free(scale_entity);
        return false;
    }

    scale_entity->view_destroy.notify = scale_handle_view_destroy;
    wl_signal_add(&view->base.events.destroy, &scale_entity->view_destroy);

    scale_entity->destroy.notify = scale_handle_transform_destroy;
    transform_add_destroy_listener(scale_entity->transform, &scale_entity->destroy);

    return true;
}
