// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>

#include <wlr/util/log.h>

#include "kywc/kycom/opengl.h"
#include "kywc/kycom/rect.h"

struct rect_render_instance {
    struct kywc_render_instance instance_base;
    struct kywc_rect_node *rect;
};

static struct rect_render_instance *
rect_render_from_render_instance(struct kywc_render_instance *instance);

static void rect_render(struct kywc_render_instance *instance, struct kywc_render_target *target,
                        pixman_region32_t *damage)
{
    struct rect_render_instance *rect_render = rect_render_from_render_instance(instance);
    if (!rect_render || !target || !damage) {
        return;
    }
    struct kywc_rect_node *rect = rect_render->rect;

    /* Rect x,y offset is plused in target. */
    struct wlr_box geometry = {
        .x = 0,
        .y = 0,
        .width = rect->width,
        .height = rect->height,
    };

    vec4 color;
    for (int i = 0; i < 4; i++) {
        color[i] = rect->color[i];
    }
    kywc_target_render_begin(target);
    kywc_target_render_rectangle(target, &geometry, color, RENDER_FLAG_CACHED);
    kywc_target_draw_damage(target, NULL, damage);
    kywc_gl_render_clear_cached();
    kywc_target_render_end(target);
}

static void rect_compute_visible(struct kywc_render_instance *instance) {}

static const struct kywc_render_instance_interface rect_render_impl = {
    .render = rect_render,
    .compute_visible = rect_compute_visible,
};

static struct rect_render_instance *
rect_render_from_render_instance(struct kywc_render_instance *instance)
{
    if (!instance || instance->impl != &rect_render_impl) {
        return NULL;
    }

    struct rect_render_instance *rect_render =
        wl_container_of(instance, rect_render, instance_base);
    return rect_render;
}

static void rect_render_instance_init(struct rect_render_instance *instance,
                                      struct kywc_rect_node *rect)
{
    if (!instance) {
        return;
    }

    instance->rect = rect;
    kywc_render_instance_init(&instance->instance_base, &rect_render_impl,
                              kywc_render_instance_handle_destroy);
}

static struct rect_render_instance *create_rect_render_instance(struct kywc_rect_node *rect)
{
    if (!rect) {
        return NULL;
    }
    struct rect_render_instance *renderer = NULL;
    renderer = calloc(sizeof(*renderer), 1);

    if (!renderer) {
        return NULL;
    }
    rect_render_instance_init(renderer, rect);
    return renderer;
}

/********************generate_render_task*************************************/
static const char *rect_name(void);

static struct kywc_rect_node *rect_from_node(const struct kywc_node *node)
{
    if (!node || node->node_name != rect_name) {
        return NULL;
    }
    struct kywc_rect_node *rect = wl_container_of(node, rect, node);
    return rect;
}

static void rect_node_generate_render_task(const struct kywc_node *node, pixman_region32_t *damage,
                                           struct kywc_render_target *target,
                                           struct wl_list *render_tasks)
{
    struct kywc_rect_node *rect = rect_from_node(node);
    if (!rect || !render_tasks || !damage || !node->enabled) {
        return;
    }
    /* local logic damage. */
    pixman_region32_t node_damage, node_opaque;
    pixman_region32_init(&node_opaque);
    pixman_region32_init(&node_damage);
    pixman_region32_intersect_rect(&node_damage, damage, 0, 0, rect->width, rect->height);

    if (!pixman_region32_not_empty(&node_damage)) {
        goto final;
        return;
    }

    if (rect->color[3] >= 1.0f) {
        node->get_opaque_region(node, &node_opaque);
        pixman_region32_intersect(&node_opaque, &node_opaque, &node_damage);
        pixman_region32_subtract(damage, damage, &node_opaque);
    }

    /* 1. Generate kywc_scene_rect's render_instance; */
    struct rect_render_instance *renderer = create_rect_render_instance(rect);
    if (!renderer) {
        goto final;
        return;
    }

    /* 2. Generate render_task, insert in render_tasks list; */
    struct kywc_render_task *task =
        kywc_render_task_create(&renderer->instance_base, &node_damage, target);
    /* Insert in the task list head. */
    wl_list_insert(render_tasks, &task->link);

final:
    pixman_region32_fini(&node_damage);
    pixman_region32_fini(&node_opaque);
}

/******************************************************************************/
static void rect_node_travel(struct kywc_node *node, struct kywc_node_visitor *visitor)
{
    if (!node) {
        return;
    }

    struct kywc_rect_node *rect_node = rect_from_node(node);
    if (!visitor || !visitor->impl || !visitor->impl->visit_rect || !rect_node || visitor->travel_break) {
        return;
    }
    visitor->flag = VISITOR_LEAF;
    visitor->lx += node->x;
    visitor->ly += node->y;

    visitor->impl->visit_rect(visitor, rect_node);
    visitor->skip_current_node = false;

    visitor->lx -= node->x;
    visitor->ly -= node->y;
}

static void rect_node_get_bounding_box(const struct kywc_node *node, struct wlr_box *box)
{
    struct kywc_rect_node *rect_node = rect_from_node(node);
    if (!box || !rect_node || !node->enabled) {
        memset(box, 0, sizeof(*box));
        return;
    }
    box->x = 0;
    box->y = 0;
    box->width = rect_node->width;
    box->height = rect_node->height;
}

static void rect_node_get_opaque_region(const struct kywc_node *node,
                                        pixman_region32_t *opaque_region)
{
    struct kywc_rect_node *rect_node = rect_from_node(node);
    if (!rect_node || !opaque_region) {
        return;
    }

    if (rect_node->color[3] >= 1.0f) {
        pixman_region32_init_rect(opaque_region, 0, 0, rect_node->width, rect_node->height);
    } else {
        pixman_region32_clear(opaque_region);
    }
}

static const char *rect_name(void)
{
    return "rectangle node";
}

void kywc_rect_node_init(struct kywc_rect_node *rect_node, int width, int height,
                         const float color[static 4])
{
    if (!rect_node) {
        return;
    }
    kywc_node_init(&rect_node->node);
    rect_node->node.generate_render_task = rect_node_generate_render_task;
    rect_node->node.travel = rect_node_travel;
    rect_node->node.get_bounding_box = rect_node_get_bounding_box;
    /* group->node.push_damage don't need override. */
    rect_node->node.get_opaque_region = rect_node_get_opaque_region;
    rect_node->node.node_name = rect_name;

    rect_node->width = width;
    rect_node->height = height;
    memcpy(rect_node->color, color, sizeof(rect_node->color));
}

struct kywc_rect_node *kywc_scene_rect_from_node(struct kywc_node *node)
{
    return rect_from_node(node);
}

struct kywc_rect_node *kywc_rect_node_create(struct kywc_group_node *parent, int width, int height,
                                             const float color[static 4])
{
    struct kywc_rect_node *rect_node = calloc(1, sizeof(struct kywc_rect_node));
    if (!rect_node) {
        return NULL;
    }
    assert(parent);

    kywc_rect_node_init(rect_node, width, height, color);
    kywc_node_add(&rect_node->node, parent);
    if (rect_node->node.push_damage) {
        rect_node->node.push_damage(&rect_node->node, NULL);
    }
    return rect_node;
}

void kywc_rect_node_set_size(struct kywc_rect_node *rect, int width, int height)
{
    if (rect->width == width && rect->height == height) {
        return;
    }
    bool bafter_update = false;
    if ((rect->width > width || rect->height > height) && rect->node.push_damage) {
        rect->node.push_damage(&rect->node, NULL);
    } else if (rect->width < width || rect->height < height) {
        bafter_update = true;
    }
    rect->width = width;
    rect->height = height;

    if (bafter_update && rect->node.push_damage) {
        rect->node.push_damage(&rect->node, NULL);
    }
}

void kywc_rect_node_set_color(struct kywc_rect_node *rect, const float color[static 4])
{
    if (memcmp(rect->color, color, sizeof(rect->color)) == 0) {
        return;
    }

    memcpy(rect->color, color, sizeof(rect->color));
    if (rect->node.push_damage) {
        rect->node.push_damage(&rect->node, NULL);
    }
}
