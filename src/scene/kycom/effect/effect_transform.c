// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#include "kywc/kycom/effect_transform.h"
#include "kywc/kycom/effect_view.h"
#include "kywc/log.h"

const char *kywc_transform_name = "transform";

struct kywc_transform_data kywc_transform_data;

static void texture_render(struct kywc_render_instance *instance, struct kywc_render_target *target,
                           pixman_region32_t *damage);

static struct kywc_transform_node_render_instance *
transform_node_render_instance_create(struct kywc_transform_geometry_node *transform_node);

const struct kywc_render_instance_interface transform_node_render_instance_impl = {
    .render = texture_render,
    .compute_visible = NULL,
    .try_direct_scanout = NULL,
};

float kywc_current_time_factor(int32_t start_time, int32_t end_time)
{
    float time_factor = (end_time - start_time) / TRANSFORM_TIME;

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

int64_t kywc_current_time_msec(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

void kywc_geometry_current_box(struct kywc_transform_data *kywc_transform_data)
{
    struct kywc_effect_view *view = kywc_transform_data->view;
    float factor = kywc_transform_data->time_factor;
    kywc_transform_data->geometry_box.x =
        view->last_box.x - (view->last_box.x - view->dst_box.x) * factor;

    kywc_transform_data->geometry_box.y =
        view->last_box.y - (view->last_box.y - view->dst_box.y) * factor;

    kywc_transform_data->geometry_box.width =
        view->last_box.width - (view->last_box.width - view->dst_box.width) * factor;

    kywc_transform_data->geometry_box.height =
        view->last_box.height - (view->last_box.height - view->dst_box.height) * factor;
}

void kywc_transform_local_damage(struct wlr_box *box,
                                 struct kywc_transform_data *kywc_transform_data)
{
    box->x = kywc_transform_data->geometry_box.x - kywc_transform_data->padding.left;
    box->y = kywc_transform_data->geometry_box.y - kywc_transform_data->padding.right;
    box->width = kywc_transform_data->geometry_box.width + kywc_transform_data->padding.top +
                 kywc_transform_data->padding.left;
    box->height = kywc_transform_data->geometry_box.height + kywc_transform_data->padding.bottom +
                  kywc_transform_data->padding.right;
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

    struct kywc_effect_view *view = render->node->kywc_transform_data->view;
    if (kywc_transform_data.view->kywc_view->need_ssd) {
        struct kywc_render_target *snap_target = kywc_effect_view_get_target(view);
        struct kywc_node *source_node = &view->view_node->node;
        kywc_transform_data.view->view_snap_buffer = &snap_target->buffer;
        struct kywc_gl_texture *gl_texture = kywc_node_generate_texture(source_node, snap_target, 1.0);
        if (!gl_texture) {
            kywc_log(KYWC_INFO, "texture is null");
        }
        kywc_transform_data.view->view_snap_buffer->fb_tex = gl_texture;
    }
    struct kywc_texture_node *tex_node =
        kywc_effect_view_get_texture_node(render->node->kywc_transform_data->view);
    // x1 is the position of the upper left corner,x2 is the position
    // of the lower right corne ,x2 needs to add the position of x1
    struct kywc_gl_geometry geometry = {
        .x1 = kywc_transform_data.geometry_box.x - kywc_transform_data.padding.left,
        .y1 = kywc_transform_data.geometry_box.y - kywc_transform_data.padding.right,
        .x2 = kywc_transform_data.geometry_box.width + kywc_transform_data.padding.top +
              kywc_transform_data.geometry_box.x,
        .y2 = kywc_transform_data.geometry_box.height + kywc_transform_data.padding.bottom +
              kywc_transform_data.geometry_box.y,
    };

    vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    if (kywc_transform_data.view->kywc_view->minimized) {
        color[3] = transform_get_alpha(kywc_transform_data.time_factor);
    }
    kywc_target_render_begin(target);
    if (kywc_transform_data.view->kywc_view->need_ssd) {
        struct kywc_gl_texture *gl_texture = kywc_transform_data.view->view_snap_buffer->fb_tex;
        kywc_target_render_texture(gl_texture, target, &geometry, color, RENDER_FLAG_CACHED);
    } else {
        kywc_target_render_texture(tex_node->texture, target, &geometry, color, RENDER_FLAG_CACHED);
    }
    kywc_target_draw_damage(target, tex_node->texture, damage);

    kywc_gl_render_texture_clear_cached();

    kywc_target_render_end(target);
}

static void node_get_bounding_box(const struct kywc_node *node, struct wlr_box *box)
{
    if (!box || !node) {
        return;
    }
    box->x = kywc_transform_data.geometry_box.x - kywc_transform_data.padding.left;
    box->y = kywc_transform_data.geometry_box.y - kywc_transform_data.padding.right;
    box->width = kywc_transform_data.geometry_box.width + kywc_transform_data.padding.top +
                 kywc_transform_data.padding.left;
    box->height = kywc_transform_data.geometry_box.height + kywc_transform_data.padding.bottom +
                  kywc_transform_data.padding.right;
}

static void node_get_opaque_region(const struct kywc_node *node,
                                   pixman_region32_t *opaque_region)
{
    struct kywc_transform_geometry_node *tg_node = wl_container_of(node, tg_node, node);

    pixman_region32_init_rect(&tg_node->opaque_region, 0, 0, 0, 0);
}

static const char *transform_geometry_node_name(void)
{
    return "kywc_transform_geometry_node";
}

static void transform_geometry_node_init(struct kywc_transform_geometry_node *tg_node,
                                         struct kywc_transform_data *kywc_transform_data)
{
    if (!tg_node || !kywc_transform_data) {
        return;
    }
    struct kywc_node *node = &tg_node->node.node;
    kywc_group_node_init(&tg_node->node);
    kywc_group_node_set_generate_func(&tg_node->node, transform_generate_render_task);
    node->node_name = transform_geometry_node_name;

    node->get_opaque_region = node_get_opaque_region;
    node->get_bounding_box = node_get_bounding_box;

    tg_node->name = kywc_transform_name;
    tg_node->kywc_transform_data = kywc_transform_data;
    tg_node->kywc_transform_data->view = kywc_transform_data->view;
    pixman_region32_init(&tg_node->opaque_region);
}

void kywc_transform_geometry_node_destroy(struct kywc_transform_geometry_node *node)
{
    if (!node) {
        return;
    }
    node->kywc_transform_data = NULL;
    pixman_region32_fini(&node->opaque_region);
    free(node);
}

struct kywc_transform_geometry_node *
kywc_transform_geometry_node_create(struct kywc_transform_data *kywc_transform_data)
{
    struct kywc_transform_geometry_node *tg_node = malloc(sizeof(*tg_node));
    if (!tg_node) {
        return NULL;
    }

    transform_geometry_node_init(tg_node, kywc_transform_data);

    return tg_node;
}