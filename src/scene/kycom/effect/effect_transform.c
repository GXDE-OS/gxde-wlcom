// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#include "kywc/kycom/effect_transform.h"
#include "kywc/kycom/effect_view.h"
#include "kywc/log.h"

const char *kywc_transform_name = "transform";

static void texture_render(struct kywc_render_instance *instance, struct kywc_render_target *target,
                           pixman_region32_t *damage);

static struct kywc_transform_node_render_instance *
transform_node_render_instance_create(struct kywc_transform_geometry_node *transform_node);

const struct kywc_render_instance_interface transform_node_render_instance_impl = {
    .render = texture_render,
    .compute_visible = NULL,
    .try_direct_scanout = NULL,
};

float kywc_calc_time_factor(int32_t start_time, int32_t current_time)
{
    float time_factor = (current_time - start_time) / TRANSFORM_TIME;

    if (time_factor > 1.0) {
        time_factor = 1.0;
    } else if (time_factor < 0.0) {
        time_factor = 0.0;
    }
    return time_factor;
}

static float transform_get_alpha(float time_factor)
{
    float current_alpha = 1 - time_factor;
    return current_alpha;
}

int64_t kywc_get_time_msec(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

void kywc_transform_data_calc_geometry(struct kywc_transform_data *data)
{
    struct kywc_box last_box = data->view->last_box;
    struct kywc_box dst_box = data->view->dst_box;
    float factor = data->time_factor;

    data->geometry_box.x = last_box.x - (last_box.x - dst_box.x) * factor;

    data->geometry_box.y = last_box.y - (last_box.y - dst_box.y) * factor;

    data->geometry_box.width = last_box.width - (last_box.width - dst_box.width) * factor;

    data->geometry_box.height = last_box.height - (last_box.height - dst_box.height) * factor;
}

void kywc_transform_data_calc_local_damage(struct kywc_transform_data *data,
                                           struct wlr_box *local_damage)
{
    struct padding padding = data->padding;
    struct kywc_box geometry_box = data->geometry_box;

    local_damage->x = geometry_box.x - padding.left;
    local_damage->y = geometry_box.y - padding.right;
    local_damage->width = geometry_box.width + padding.top + padding.left;
    local_damage->height = geometry_box.height + padding.bottom + padding.right;
}

static struct kywc_transform_node_render_instance *
transform_node_render_instance_create(struct kywc_transform_geometry_node *transform_node)
{
    struct kywc_transform_node_render_instance *render = malloc(sizeof(*render));
    if (!render) {
        return NULL;
    }

    render->node = transform_node;

    kywc_render_instance_init(&render->base, &transform_node_render_instance_impl,
                              kywc_render_instance_handle_destroy);

    return render;
}

/********************generate_render_task*******************************/
static void transform_generate_render_task(const struct kywc_node *node, pixman_region32_t *damage,
                                           struct kywc_render_target *target,
                                           struct wl_list *render_tasks)
{
    struct kywc_transform_geometry_node *transform_node =
        wl_container_of(node, transform_node, node);
    if (!render_tasks || !damage || !node->enabled) {
        return;
    }
    struct wlr_box bound_box;
    node->get_bounding_box(node, &bound_box);
    pixman_region32_t node_damage, node_opaque;
    pixman_region32_init(&node_damage);
    pixman_region32_init(&node_opaque);
    pixman_region32_intersect_rect(&node_damage, damage, bound_box.x, bound_box.y, bound_box.width,
                                   bound_box.height);

    if (!pixman_region32_not_empty(&node_damage)) {
        goto final;
        return;
    }
    node->get_opaque_region(node, &transform_node->opaque_region);

    struct kywc_transform_node_render_instance *node_render =
        transform_node_render_instance_create(transform_node);

    struct kywc_render_task *task =
        kywc_render_task_create(&node_render->base, &node_damage, target);
    wl_list_insert(render_tasks, &task->link);

final:
    pixman_region32_fini(&node_damage);
    pixman_region32_fini(&node_opaque);
}

static void texture_render(struct kywc_render_instance *instance, struct kywc_render_target *target,
                           pixman_region32_t *damage)
{
    struct kywc_transform_node_render_instance *render = wl_container_of(instance, render, base);

    struct kywc_transform_data *data = render->node->data;
    struct kywc_effect_view *view = data->view;
    struct padding padding = data->padding;
    struct kywc_box geometry_box = data->geometry_box;
    struct kywc_gl_texture *gl_texture;
    if (view->kywc_view->ssd == KYWC_SSD_ALL) {
        struct kywc_render_target *snap_target = kywc_effect_view_get_target(view);
        struct kywc_node *source_node = &view->view_node->node;
        gl_texture = kywc_node_generate_texture(source_node, snap_target, 1.0);
        if (!gl_texture) {
            kywc_log(KYWC_INFO, "texture is null");
        }
    } else {
        struct kywc_texture_node *tex_node = kywc_effect_view_get_texture_node(view);
        gl_texture = tex_node->texture;
    }
    // struct kywc_texture_node *tex_node = kywc_effect_view_get_texture_node(view);
    //  x1 is the position of the upper left corner,x2 is the position
    //  of the lower right corne ,x2 needs to add the position of x1
    struct kywc_gl_geometry geometry = {
        .x1 = geometry_box.x - padding.left,
        .y1 = geometry_box.y - padding.right,
        .x2 = geometry_box.width + padding.top + geometry_box.x,
        .y2 = geometry_box.height + padding.bottom + geometry_box.y,
    };

    vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    if (view->kywc_view->minimized) {
        color[3] = transform_get_alpha(data->time_factor);
    }
    kywc_target_render_begin(target);

    kywc_target_render_texture(gl_texture, target, &geometry, color, RENDER_FLAG_CACHED);

    kywc_target_draw_damage(target, gl_texture, damage);

    kywc_gl_render_texture_clear_cached();

    kywc_target_render_end(target);
}

static void node_get_bounding_box(const struct kywc_node *node, struct wlr_box *box)
{
    if (!box || !node) {
        return;
    }
    struct kywc_transform_geometry_node *tg_node = wl_container_of(node, tg_node, node);
    struct padding padding = tg_node->data->padding;
    struct kywc_box geometry_box = tg_node->data->geometry_box;

    box->x = geometry_box.x - padding.left;
    box->y = geometry_box.y - padding.right;
    box->width = geometry_box.width + padding.top + padding.left;
    box->height = geometry_box.height + padding.bottom + padding.right;
}

static void node_get_opaque_region(const struct kywc_node *node, pixman_region32_t *opaque_region)
{
    pixman_region32_clear(opaque_region);
}

static const char *transform_geometry_node_name(void)
{
    return "kywc_transform_geometry_node";
}

static void transform_geometry_node_init(struct kywc_transform_geometry_node *tg_node,
                                         struct kywc_transform_data *data)
{
    if (!tg_node || !data) {
        return;
    }
    struct kywc_node *node = &tg_node->node.node;
    kywc_group_node_init(&tg_node->node);
    kywc_group_node_set_generate_func(&tg_node->node, transform_generate_render_task);
    node->node_name = transform_geometry_node_name;

    node->get_opaque_region = node_get_opaque_region;
    node->get_bounding_box = node_get_bounding_box;

    tg_node->data = data;
    pixman_region32_init(&tg_node->opaque_region);
}

void kywc_transform_data_calc_padding_region(struct kywc_transform_data *data,
                                             struct kywc_effect_view *view)
{
    struct kywc_view *ky_view = view->kywc_view;
    if (!ky_view) {
        return;
    }
    struct kywc_box *geometry_box = &data->geometry_box;
    struct padding *shadow_box = &data->shadow_box;
    struct padding *padding_box = &data->padding;

    if (ky_view->ssd == KYWC_SSD_ALL) {
        float width_scale =
            1.0 * geometry_box->width / (ky_view->geometry.width + ky_view->margin.off_width);
        float height_scale =
            1.0 * geometry_box->height / (ky_view->geometry.height + ky_view->margin.off_height);
        if (ky_view->maximized) {
            memset(padding_box, 0, sizeof(*padding_box));
        } else {
            padding_box->left = width_scale * shadow_box->left;
            padding_box->right = width_scale * shadow_box->right;
            padding_box->top = ceil(width_scale * shadow_box->top);
            padding_box->bottom = ceil(height_scale * shadow_box->bottom);
        }
    } else {
        float width_scale = 1.0 * geometry_box->width / ky_view->geometry.width;
        float height_scale = 1.0 * geometry_box->height / ky_view->geometry.height;
        padding_box->left = width_scale * ky_view->padding.left;
        padding_box->right = width_scale * ky_view->padding.right;
        padding_box->top = ceil(width_scale * ky_view->padding.top);
        padding_box->bottom = ceil(height_scale * ky_view->padding.bottom);
    }
}

void kywc_transform_data_calc_shadow(struct kywc_transform_data *data, struct kywc_box *bound_box)
{
    struct kywc_effect_view *view = data->view;
    if (!view || view->kywc_view->ssd != KYWC_SSD_ALL) {
        return;
    }
    struct padding *shadow_box = &data->shadow_box;
    struct kywc_view *_view = view->kywc_view;
    shadow_box->top = (bound_box->width - _view->geometry.width - _view->margin.off_width) / 2;
    shadow_box->bottom =
        (bound_box->height - _view->geometry.height - _view->margin.off_height) / 2;
    shadow_box->left = shadow_box->top;
    shadow_box->right = shadow_box->top;
}

void kywc_transform_geometry_node_destroy(struct kywc_transform_geometry_node *node)
{
    if (!node) {
        return;
    }
    node->data = NULL;
    pixman_region32_fini(&node->opaque_region);
    free(node);
}

struct kywc_transform_geometry_node *
kywc_transform_geometry_node_create(struct kywc_transform_data *data)
{
    struct kywc_transform_geometry_node *tg_node = malloc(sizeof(*tg_node));
    if (!tg_node) {
        return NULL;
    }

    transform_geometry_node_init(tg_node, data);

    return tg_node;
}
