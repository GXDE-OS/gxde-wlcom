// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#include "kywc/kycom/effect_transform.h"
#include "kywc/kycom/effect_view.h"
#include "kywc/log.h"

static void geometry_transform_node_render(struct kywc_render_instance *instance,
                                           struct kywc_render_target *target,
                                           pixman_region32_t *damage);

static struct kywc_geometry_transform_render_instance *
geometry_transform_node_render_instance_create(struct kywc_geometry_transform_node *transform_node);

const struct kywc_render_instance_interface transform_node_render_instance_impl = {
    .render = geometry_transform_node_render,
    .compute_visible = NULL,
    .try_direct_scanout = NULL,
};

int64_t kywc_get_current_time_msec(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void xy_linear_func_init(struct xy_linear_func *func, float start_factor, float start_value,
                                float end_factor, float end_value)
{
    if (!func) {
        return;
    }
    func->start_point[0] = start_factor;
    func->start_point[1] = start_value;
    func->end_point[0] = end_factor;
    func->end_point[1] = end_value;

    if (func->end_point[0] - func->start_point[0] == 0) {
        func->k = 0;
    } else {
        func->k = (func->end_point[1] - func->start_point[1]) /
                  (func->end_point[0] - func->start_point[0]);
    }

    func->b = func->start_point[1] - func->k * func->start_point[0];
}

static float xy_linear_func_get_value(struct xy_linear_func *func, float time_factor)
{
    if (!func) {
        return 0;
    }

    return time_factor * func->k + func->b;
}

static void transform_data_init(struct kywc_transform_data *data)
{
    data->start_time = 0;
    data->time = -1;
    data->time_factor = -1;
    data->alpha = -1;
    data->max_time_factor = 1;
    data->view = NULL;
    data->view_node = NULL;
    memset(&data->padding, 0, sizeof(data->padding));
    memset(&data->view_box, 0, sizeof(data->view_box));
    memset(&data->dst_box, 0, sizeof(data->dst_box));
}

float kywc_transform_data_calc_time_factor(struct kywc_transform_data *data)
{
    float time_factor = (data->time - data->start_time) / TRANSFORM_TIME;

    if (time_factor > data->max_time_factor) {
        time_factor = data->max_time_factor;
    } else if (time_factor < 0.0) {
        time_factor = 0.0;
    }
    data->time_factor = time_factor;
    return time_factor;
}

void kywc_transform_data_alpha_func_init(struct kywc_transform_data *data, float end_alpha)
{
    xy_linear_func_init(&data->alpha_func, data->time_factor, data->alpha, data->max_time_factor,
                        end_alpha);
}

void kywc_transform_data_view_box_func_init(struct kywc_transform_data *data)
{
    struct kywc_effect_view *view = data->view;
    float factor = data->time_factor;
    float max_factor = data->max_time_factor;
    float start_value[4];
    float end_value[4];

    if (data->time == -1) {
        // start transform
        data->view_box = view->last_box;
    }

    start_value[0] = data->view_box.x;
    start_value[1] = data->view_box.y;
    start_value[2] = data->view_box.width;
    start_value[3] = data->view_box.height;

    end_value[0] = view->dst_box.x;
    end_value[1] = view->dst_box.y;
    end_value[2] = view->dst_box.width;
    end_value[3] = view->dst_box.height;
    for (int i = 0; i < 4; i++) {
        xy_linear_func_init(data->view_box_func + i, factor, start_value[i], max_factor,
                            end_value[i]);
    }
}

void kywc_transform_data_calc_alpha(struct kywc_transform_data *data)
{
    data->alpha = xy_linear_func_get_value(&data->alpha_func, data->time_factor);
}

void kywc_transform_data_calc_view_box(struct kywc_transform_data *data)
{
    float factor = data->time_factor;

    data->view_box.x = xy_linear_func_get_value(data->view_box_func, factor);

    data->view_box.y = xy_linear_func_get_value(data->view_box_func + 1, factor);

    data->view_box.width = xy_linear_func_get_value(data->view_box_func + 2, factor);

    data->view_box.height = xy_linear_func_get_value(data->view_box_func + 3, factor);
}

void kywc_transform_data_update_location(struct kywc_transform_data *data)
{
    struct padding *padding = &data->padding;
    struct kywc_box *view_box = &data->view_box;
    struct kywc_box *dst_box = &data->dst_box;
    int lx, ly;
    struct kywc_group_node *group_node = kywc_node_get_parent(&data->view_node->node);
    kywc_node_coords(&group_node->node, &lx, &ly);
    dst_box->x = view_box->x - padding->left;
    dst_box->y = view_box->y - padding->right;
    dst_box->width = view_box->width + padding->top + padding->left;
    dst_box->height = view_box->height + padding->bottom + padding->right;
    data->x = dst_box->x - lx;
    data->y = dst_box->y - ly;
}

void kywc_transform_data_calc_padding_region(struct kywc_transform_data *data,
                                             struct kywc_effect_view *view)
{
    struct kywc_view *ky_view = view->kywc_view;
    if (!ky_view) {
        return;
    }
    struct kywc_box *view_box = &data->view_box;
    struct padding *shadow = &data->view->shadow;
    struct padding *transform_padding = &data->padding;

    float width_scale, height_scale;
    if (ky_view->ssd == KYWC_SSD_ALL) {
        width_scale = 1.0 * view_box->width / (ky_view->geometry.width + ky_view->margin.off_width);
        height_scale =
            1.0 * view_box->height / (ky_view->geometry.height + ky_view->margin.off_height);
    } else {
        width_scale = 1.0 * view_box->width / ky_view->geometry.width;
        height_scale = 1.0 * view_box->height / ky_view->geometry.height;
    }

    transform_padding->left = width_scale * shadow->left;
    transform_padding->right = width_scale * shadow->right;
    transform_padding->top = ceil(width_scale * shadow->top);
    transform_padding->bottom = ceil(height_scale * shadow->bottom);
}

/********************generate_render_task*******************************/
static struct kywc_geometry_transform_render_instance *
geometry_transform_node_render_instance_create(struct kywc_geometry_transform_node *transform_node)
{
    struct kywc_geometry_transform_render_instance *render = malloc(sizeof(*render));
    if (!render) {
        return NULL;
    }

    render->node = transform_node;

    kywc_render_instance_init(&render->base, &transform_node_render_instance_impl,
                              kywc_render_instance_handle_destroy);

    return render;
}

static void geometry_transform_node_generate_render_task(const struct kywc_node *node,
                                                         pixman_region32_t *damage,
                                                         struct kywc_render_target *target,
                                                         struct wl_list *render_tasks)
{
    struct kywc_geometry_transform_node *transform_node =
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

    struct kywc_geometry_transform_render_instance *node_render =
        geometry_transform_node_render_instance_create(transform_node);

    struct kywc_render_task *task =
        kywc_render_task_create(&node_render->base, &node_damage, target);
    wl_list_insert(render_tasks, &task->link);

final:
    pixman_region32_fini(&node_damage);
    pixman_region32_fini(&node_opaque);
}

static void geometry_transform_node_render(struct kywc_render_instance *instance,
                                           struct kywc_render_target *target,
                                           pixman_region32_t *damage)
{
    struct kywc_geometry_transform_render_instance *render =
        wl_container_of(instance, render, base);

    struct kywc_transform_data *data = &render->node->data;
    struct kywc_effect_view *view = data->view;
    struct padding padding = data->padding;
    struct kywc_box view_box = data->view_box;
    struct kywc_gl_texture *gl_texture;
    if (view->kywc_view->ssd == KYWC_SSD_ALL || view->kywc_view->shaded) {
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
    //  x1 is the position of the upper left corner,x2 is the position
    //  of the lower right corne ,x2 needs to add the position of x1
    struct kywc_gl_geometry geometry = {
        .x1 = view_box.x - padding.left,
        .y1 = view_box.y - padding.right,
        .x2 = view_box.width + padding.top + view_box.x,
        .y2 = view_box.height + padding.bottom + view_box.y,
    };

    vec4 color = { 1.0f, 1.0f, 1.0f, data->alpha };

    kywc_target_render_begin(target);

    kywc_target_render_texture(gl_texture, target, &geometry, color, RENDER_FLAG_CACHED);

    kywc_target_draw_damage(target, gl_texture, damage);

    kywc_gl_render_texture_clear_cached();

    kywc_target_render_end(target);
}

/********************kywc_geometry_transform_node*******************************/
static void geometry_transform_node_get_bounding_box(const struct kywc_node *node,
                                                     struct wlr_box *box)
{
    if (!box || !node) {
        return;
    }
    struct kywc_geometry_transform_node *tg_node = wl_container_of(node, tg_node, node);
    struct kywc_transform_data *data = &tg_node->data;
    box->x = data->x;
    box->y = data->y;
    box->width = data->dst_box.width;
    box->height = data->dst_box.height;
}

static void geometry_transform_node_get_opaque_region(const struct kywc_node *node,
                                                      pixman_region32_t *opaque_region)
{
    pixman_region32_clear(opaque_region);
}

static const char *geometry_transform_node_name(void)
{
    return "kywc_geometry_transform_node";
}

static void geometry_transform_node_destroy(struct kywc_node *node)
{
    if (!node) {
        return;
    }
    struct kywc_geometry_transform_node *tg_node = wl_container_of(node, tg_node, node);

    pixman_region32_fini(&tg_node->opaque_region);
    tg_node->group_destroy(node);
}

static void transform_geometry_node_init(struct kywc_geometry_transform_node *tg_node,
                                         struct kywc_transform_data *data)
{
    if (!tg_node || !data) {
        return;
    }
    struct kywc_node *node = &tg_node->node.node;
    kywc_group_node_init(&tg_node->node);
    kywc_group_node_set_generate_func(&tg_node->node, geometry_transform_node_generate_render_task);
    node->node_name = geometry_transform_node_name;
    tg_node->group_destroy = node->destroy;
    node->destroy = geometry_transform_node_destroy;
    node->get_opaque_region = geometry_transform_node_get_opaque_region;
    node->get_bounding_box = geometry_transform_node_get_bounding_box;

    pixman_region32_init(&tg_node->opaque_region);
}

struct kywc_geometry_transform_node *
kywc_transform_geometry_node_create(struct kywc_effect_view *view)
{
    struct kywc_geometry_transform_node *tg_node = malloc(sizeof(*tg_node));
    if (!tg_node) {
        return NULL;
    }
    struct kywc_transform_data *data = &tg_node->data;
    transform_data_init(data);
    data->view = view;
    data->view_node = view->view_node;
    data->start_time = kywc_get_current_time_msec();

    transform_geometry_node_init(tg_node, data);

    return tg_node;
}
