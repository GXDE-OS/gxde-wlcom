// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>
#include <wlr/util/region.h>

#include "kywc/kycom/scene.h"
#include "kywc/kycom/texture.h"
#include "scene/kycom/effects_impl.h"
#include "scene/kycom/opengl_p.h"

#include "server.h"

void node_output_update(struct kywc_node *node);

struct finding_node_visitor {
    struct kywc_node_visitor base;
    struct kywc_node *finded_node;
    double point_lx, point_ly;
    int nx, ny;
    bool skip_current_node;
};

struct finding_node_visitor *finding_visitor_from_node_visitor(struct kywc_node_visitor *visitor);

/********************render_instance******************************************/
void kywc_render_instance_handle_destroy(struct wl_listener *listener, void *data)
{
    struct kywc_render_instance *render_instance =
        wl_container_of(listener, render_instance, destroy_listener);

    wl_signal_emit_mutable(&render_instance->event.destroy, NULL);
    wl_list_remove(&render_instance->destroy_listener.link);
    free(render_instance);
}

void kywc_render_instance_init(struct kywc_render_instance *instance,
                               const struct kywc_render_instance_interface *impl,
                               wl_notify_func_t destroy)
{
    if (!instance || !impl) {
        return;
    }

    assert(destroy);

    instance->impl = impl;
    wl_signal_init(&instance->event.destroy);

    instance->destroy_listener.notify = destroy;
}

/********************render_task*********************/
static void render_task_destroy(struct kywc_render_task *task)
{
    if (!task) {
        return;
    }

    pixman_region32_fini(&task->damage);
    if (task->target) {
        free(task->target);
    }
    free(task);
}

void kywc_render_task_init(struct kywc_render_task *task, struct kywc_render_instance *instance,
                           const pixman_region32_t *damage, const struct kywc_render_target *target)
{
    if (!task) {
        return;
    }
    task->instance = instance;
    pixman_region32_init(&task->damage);
    pixman_region32_copy(&task->damage, damage);

    task->target = calloc(sizeof(*task->target), 1);
    if (task->target) {
        kywc_target_cpy(task->target, target);
    }

    wl_list_init(&task->link);
    wl_list_init(&task->dependences);

    wl_signal_init(&task->event.destroy);
    /* kywc_render_instance release with task. */
    wl_signal_add(&task->event.destroy, &instance->destroy_listener);
}

struct kywc_render_task *kywc_render_task_create(struct kywc_render_instance *instance,
                                                 const pixman_region32_t *damage,
                                                 const struct kywc_render_target *target)
{
    struct kywc_render_task *task = calloc(sizeof(*task), 1);
    if (!task) {
        return NULL;
    }

    kywc_render_task_init(task, instance, damage, target);
    return task;
}

void kywc_render_task_release(struct kywc_render_task *task)
{
    if (!task) {
        return;
    }
    wl_signal_emit_mutable(&task->event.destroy, task);
    render_task_destroy(task);
}

/**********************************kywc_node*************************************/
static void finding_visitor_node(struct kywc_node_visitor *visitor, struct kywc_node *node);

static void finding_visitor_rect_node(struct kywc_node_visitor *visitor,
                                      struct kywc_rect_node *node);

static void finding_visitor_texture_node(struct kywc_node_visitor *visitor,
                                         struct kywc_texture_node *node);

const struct kywc_node_visitor_interface finding_visitor_impl = {
    .visit_node = finding_visitor_node,
    .visit_group = NULL, /*Don't need judge in group node*/
    .visit_rect = finding_visitor_rect_node,
    .visit_texture = finding_visitor_texture_node,
};

static struct finding_node_visitor finding_visitor;

static void finding_visitor_init(int cursor_point_x, int cursor_point_y)
{
    finding_visitor.point_lx = cursor_point_x;
    finding_visitor.point_ly = cursor_point_y;
    finding_visitor.nx = 0;
    finding_visitor.ny = 0;
    finding_visitor.skip_current_node = false;
    finding_visitor.base.lx = 0;
    finding_visitor.base.ly = 0;
    finding_visitor.base.bypass = false;
    finding_visitor.base.flag = VISITOR_LEAF;
    finding_visitor.base.impl = &finding_visitor_impl;
}

struct finding_node_visitor *finding_visitor_from_node_visitor(struct kywc_node_visitor *visitor)
{
    struct finding_node_visitor *find_visitor = wl_container_of(visitor, find_visitor, base);
    if (find_visitor->base.impl != &finding_visitor_impl) {
        return NULL;
    }

    return find_visitor;
}

static void finding_visitor_node(struct kywc_node_visitor *visitor, struct kywc_node *node)
{
    if (node->visiting_callback) {
        node->visiting_callback(node, visitor);
    }

    struct finding_node_visitor *find_visitor = finding_visitor_from_node_visitor(visitor);

    if (find_visitor->skip_current_node) {
        find_visitor->skip_current_node = false;
        return;
    }
    int current_lx = visitor->lx;
    int current_ly = visitor->ly;
    struct wlr_box node_box;
    node->get_bounding_box(node, &node_box);
    node_box.x += current_lx;
    node_box.y += current_ly;

    if (wlr_box_contains_point(&node_box, find_visitor->point_lx, find_visitor->point_ly)) {
        find_visitor->finded_node = node;
        find_visitor->nx = floor(find_visitor->point_lx) - node_box.x;
        find_visitor->ny = floor(find_visitor->point_ly) - node_box.y;
        find_visitor->base.bypass = true;
    }
}

static void finding_visitor_rect_node(struct kywc_node_visitor *visitor,
                                      struct kywc_rect_node *node)
{
    finding_visitor_node(visitor, &node->node);
}

static void finding_visitor_texture_node(struct kywc_node_visitor *visitor,
                                         struct kywc_texture_node *node)
{
    finding_visitor_node(visitor, &node->node);
    if (!visitor->bypass) {
        return;
    }
    /* Point in texture_node */
    struct finding_node_visitor *find_visitor = wl_container_of(visitor, find_visitor, base);

    /* Point in input region */
    if (node->point_accepts_input &&
        !node->point_accepts_input(node, find_visitor->nx, find_visitor->ny)) {
        visitor->bypass = false;
        find_visitor->finded_node = NULL;
        find_visitor->nx = 0;
        find_visitor->ny = 0;
    }
}

struct kywc_node *kywc_node_at(struct kywc_node *node, double lx, double ly, double *nx, double *ny)
{
    finding_visitor_init(lx, ly);
    node->travel(node, &finding_visitor.base);

    if (finding_visitor.base.bypass && finding_visitor.finded_node) {
        if (nx) {
            *nx = finding_visitor.nx;
        }
        if (ny) {
            *ny = finding_visitor.ny;
        }
        return finding_visitor.finded_node;
    }
    return NULL;
}

static void node_travel(struct kywc_node *node, struct kywc_node_visitor *visitor)
{
    if (!node || !node->enabled) {
        return;
    }

    if (!visitor || !visitor->impl || !visitor->impl->visit_node || visitor->bypass) {
        return;
    }
    visitor->flag = VISITOR_LEAF;
    visitor->lx += node->x;
    visitor->ly += node->y;

    visitor->impl->visit_node(visitor, node);

    visitor->lx -= node->x;
    visitor->ly -= node->y;
}

static void node_push_damage(struct kywc_node *node, const struct wlr_box *local_damage)
{
    if (!node) {
        return;
    }

    struct wlr_box whole_local_damage = {
        .x = 0,
        .y = 0,
        .height = 0,
        .width = 0,
    };
    if (local_damage) {
        memcpy(&whole_local_damage, local_damage, sizeof(whole_local_damage));
    } else if (node->get_bounding_box) {
        node->get_bounding_box(node, &whole_local_damage);
    }

    whole_local_damage.x += node->x;
    whole_local_damage.y += node->y;
    if (wlr_box_empty(&whole_local_damage)) {
        return;
    }

    if (node->parent) {
        node->parent->node.push_damage(&node->parent->node, &whole_local_damage);
    }
}

static void node_get_bounding_box(const struct kywc_node *node, struct wlr_box *box)
{
    memset(box, 0, sizeof(*box));
}

static void node_get_opaque_region(const struct kywc_node *node, pixman_region32_t *opaque_region)
{
    wlr_log(WLR_ERROR, "Need to implement opaque region interface!");
    assert(false);
}

static void node_generate_render(const struct kywc_node *node, pixman_region32_t *damage,
                                 struct kywc_render_target *target, struct wl_list *render_tasks)
{
    wlr_log(WLR_ERROR, "Need to implement generate render interface!");
    assert(false);
}

static void node_destroy(struct kywc_node *node)
{
    if (!node) {
        return;
    }

    wlr_addon_set_finish(&node->addons);

    kywc_node_set_enabled(node, false);

    wl_list_remove(&node->link);
    free(node);
}

static struct kywc_node *node_get_proxy_node(struct kywc_node *node)
{
    return node->proxy_node ? node->proxy_node : node;
}

static const char *node_name(void)
{
    return "base_node";
}

/* Doesn't add damage region*/
void kywc_node_init(struct kywc_node *node)
{
    memset(node, 0, sizeof(*node));
    node->parent = NULL;
    node->enabled = true;
    node->travel = node_travel;
    node->node_name = node_name;
    node->destroy = node_destroy;
    node->push_damage = node_push_damage;
    node->get_bounding_box = node_get_bounding_box;
    node->generate_render_task = node_generate_render;
    node->get_opaque_region = node_get_opaque_region;

    wl_list_init(&node->link);
    wl_signal_init(&node->events.destroy);

    wlr_addon_set_init(&node->addons);
}

/* Doesn't add damage region*/
void kywc_node_add(struct kywc_node *node, struct kywc_group_node *parent)
{
    if (!parent || !node) {
        return;
    }
    node = node_get_proxy_node(node);
    /* Children is list head, pre is tail , Z order: head is '-' -> tail is '+'. */

    node->parent = parent;
    wl_list_insert(parent->children.prev, &node->link);
}

void kywc_node_destroy(struct kywc_node *node)
{
    if (!node) {
        return;
    }
    /**
     * We want to call the destroy listeners before we do anything else
     * in case the destroy signal would like to remove children before they
     * are recursively destroyed.
     * Destroy node must emit signal first,
     * the node_destroy doesn't emit destroy signal.
     */
    wl_signal_emit_mutable(&node->events.destroy, NULL);
    node->destroy(node);
}

void kywc_node_set_enabled(struct kywc_node *node, bool enabled)
{
    if (node->enabled == enabled) {
        return;
    }
    node->push_damage(node, NULL);
    node->enabled = enabled;
    node->push_damage(node, NULL);
}

void kywc_node_set_position(struct kywc_node *node, int x, int y)
{
    if (node->x == x && node->y == y) {
        return;
    }
    node->push_damage(node, NULL);

    node->x = x;
    node->y = y;
    node_output_update(node);
    node->push_damage(node, NULL);
}

static void finding_node_visiting_callback(struct kywc_node *node,
                                           struct kywc_node_visitor *visitor)
{
    struct finding_node_visitor *find = finding_visitor_from_node_visitor(visitor);
    if (find) {
        find->skip_current_node = true;
    }
}

void kywc_node_set_bypassed(struct kywc_node *node, bool bypassed)
{
    node->visiting_callback = bypassed ? finding_node_visiting_callback : NULL;
}

void kywc_node_place_above(struct kywc_node *node, struct kywc_node *sibling)
{
    node = node_get_proxy_node(node);
    sibling = node_get_proxy_node(sibling);
    assert(node != sibling);
    assert(node->parent == sibling->parent);

    if (node->link.prev == &sibling->link) {
        return;
    }

    node->push_damage(node, NULL);

    wl_list_remove(&node->link);
    /* After sibling, zord '-' to '+',above sibling. */
    wl_list_insert(&sibling->link, &node->link);

    node->push_damage(node, NULL);
}

void kywc_node_place_below(struct kywc_node *node, struct kywc_node *sibling)
{
    node = node_get_proxy_node(node);
    sibling = node_get_proxy_node(sibling);
    assert(node != sibling);
    assert(node->parent == sibling->parent);

    if (node->link.next == &sibling->link) {
        return;
    }

    node->push_damage(node, NULL);

    wl_list_remove(&node->link);
    /* Before sibling, zord '-' to '+', below sibling. */
    wl_list_insert(sibling->link.prev, &node->link);

    node->push_damage(node, NULL);
}

void kywc_node_raise_to_top(struct kywc_node *node)
{
    node = node_get_proxy_node(node);
    struct kywc_node *current_top = wl_container_of(node->parent->children.prev, current_top, link);
    if (node == current_top) {
        return;
    }

    kywc_node_place_above(node, current_top);
}

void kywc_node_lower_to_bottom(struct kywc_node *node)
{
    node = node_get_proxy_node(node);
    struct kywc_node *current_bottom =
        wl_container_of(node->parent->children.next, current_bottom, link);
    if (node == current_bottom) {
        return;
    }

    kywc_node_place_below(node, current_bottom);
}

void kywc_node_reparent_ex(struct kywc_node *node, struct kywc_group_node *new_parent)
{
    assert(new_parent != NULL);

    if (node->parent == new_parent) {
        return;
    }

    /* Ensure that a node cannot become its own ancestor. */
    for (struct kywc_group_node *ancestor = new_parent; ancestor != NULL;
         ancestor = ancestor->node.parent) {
        assert(&ancestor->node != node);
    }

    node->push_damage(node, NULL);

    wl_list_remove(&node->link);
    node->parent = new_parent;
    wl_list_insert(new_parent->children.prev, &node->link);

    node->push_damage(node, NULL);
}

void kywc_node_reparent(struct kywc_node *node, struct kywc_group_node *new_parent)
{
    node = node_get_proxy_node(node);
    kywc_node_reparent_ex(node, new_parent);
}

struct kywc_group_node *kywc_node_get_parent(struct kywc_node *node)
{
    return node_get_proxy_node(node)->parent;
}

bool kywc_node_coords(struct kywc_node *node, int *lx, int *ly)
{
    if (!node || !lx || !ly) {
        return false;
    }
    *lx = 0, *ly = 0;
    bool enabled = true;
    struct kywc_node *temp_node = node;
    while (temp_node && temp_node->parent) {
        enabled = enabled && temp_node->enabled;
        *lx += temp_node->x;
        *ly += temp_node->y;

        temp_node = &temp_node->parent->node;
    }

    return enabled;
}

struct kywc_root *kywc_node_get_root(struct kywc_node *node)
{
    struct kywc_group_node *group_node = node->parent;
    if (!group_node) {
        group_node = kywc_group_node_from_node(node);
    } else {
        while (group_node->node.parent != NULL) {
            group_node = group_node->node.parent;
        }
    }
    struct kywc_root *root = wl_container_of(group_node, root, group);
    return root;
}

/******************************************************************************/
struct kywc_group_node *kywc_group_node_from_node(const struct kywc_node *node)
{
    struct kywc_group_node *group = wl_container_of(node, group, node);
    return group;
}

static void group_node_destroy(struct kywc_node *node)
{
    struct kywc_group_node *group = kywc_group_node_from_node(node);
    if (!group) {
        return;
    }

    struct kywc_node *child, *child_tmp;
    wl_list_for_each_safe(child, child_tmp, &group->children, link) {
        kywc_node_destroy(child);
    }
    node_destroy(node);
}

static void group_node_travel(struct kywc_node *node, struct kywc_node_visitor *visitor)
{
    if (!node || !node->enabled) {
        return;
    }

    struct kywc_group_node *group = kywc_group_node_from_node(node);

    if (!visitor || !group || visitor->bypass) {
        return;
    }

    visitor->flag = VISITOR_BFs;
    visitor->lx += node->x;
    visitor->ly += node->y;
    if (visitor->impl && visitor->impl->visit_group) {
        visitor->impl->visit_group(visitor, group);
    }

    struct kywc_node *child;
    /* z order + -> - */
    wl_list_for_each_reverse(child, &group->children, link) {
        child->travel(child, visitor);
    }

    visitor->flag = VISITOR_DFs;
    if (visitor->impl && visitor->impl->visit_group) {
        visitor->impl->visit_group(visitor, group);
    }
    visitor->lx -= node->x;
    visitor->ly -= node->y;
}

static int max(int max1, int max2)
{
    return max1 > max2 ? max1 : max2;
}

static void group_node_get_bounding_box(const struct kywc_node *node, struct wlr_box *box)
{
    struct kywc_group_node *group = kywc_group_node_from_node(node);
    if (!group || !box || !node->enabled) {
        memset(box, 0, sizeof(*box));
        return;
    }
    if (wl_list_empty(&group->children)) {
        memset(box, 0, sizeof(*box));
    }

    int min_x = INT_MAX;
    int min_y = INT_MAX;
    int max_x2 = INT_MIN;
    int max_y2 = INT_MIN;

    struct wlr_box current_box;
    struct kywc_node *current_node, *temp_node;
    wl_list_for_each_safe(current_node, temp_node, &group->children, link) {
        if (current_node->get_bounding_box) {
            current_node->get_bounding_box(current_node, &current_box);
            current_box.x += current_node->x;
            current_box.y += current_node->y;
        } else {
            memset(&current_box, 0, sizeof(current_box));
        }
        min_x = min_x > current_box.x ? current_box.x : min_x;
        min_y = min_y > current_box.y ? current_box.y : min_y;
        max_x2 = max(max_x2, current_box.x + current_box.width);
        max_y2 = max(max_y2, current_box.y + current_box.height);
    }
    box->x = min_x == INT_MAX ? 0 : min_x;
    box->y = min_y == INT_MAX ? 0 : min_y;
    box->width = max_x2 == INT_MIN ? 0 : max_x2 - min_x;
    box->height = max_y2 == INT_MIN ? 0 : max_y2 - min_y;
}

static void group_node_get_opaque_region(const struct kywc_node *node,
                                         pixman_region32_t *opaque_region)
{
    struct kywc_group_node *group = kywc_group_node_from_node(node);
    if (!group || !opaque_region) {
        return;
    }
    pixman_region32_clear(opaque_region);

    pixman_region32_t child_opaque_region;
    pixman_region32_init(&child_opaque_region);
    struct kywc_node *current_node, *temp_node;
    wl_list_for_each_safe(current_node, temp_node, &group->children, link) {
        if (!current_node->get_opaque_region) {
            continue;
        }
        /* Start point(0,0) is group top_elft. */
        current_node->get_opaque_region(current_node, &child_opaque_region);
        if (!pixman_region32_not_empty(&child_opaque_region)) {
            continue;
        }
        pixman_region32_translate(&child_opaque_region, current_node->x, current_node->y);
        pixman_region32_union(opaque_region, opaque_region, &child_opaque_region);
    }
    pixman_region32_fini(&child_opaque_region);
}

static void group_node_generate_render(const struct kywc_node *node, pixman_region32_t *damage,
                                       struct kywc_render_target *target,
                                       struct wl_list *render_tasks)
{
    if (!node->enabled) {
        return;
    }

    struct kywc_group_node *group = wl_container_of(node, group, node);
    kywc_group_node_children_generate_render(group, damage, target, render_tasks);
}

static const char *group_node_name(void)
{
    return "base_group";
}

void kywc_group_node_children_generate_render(const struct kywc_group_node *group,
                                              pixman_region32_t *damage,
                                              struct kywc_render_target *target,
                                              struct wl_list *render_tasks)
{
    /**
     * Damage follow target view_box.
     * Damage origin at surface top-left corner.
     */
    if (!group || !group->node.enabled) {
        return;
    }

    struct kywc_node *current_node, *temp_node;
    wl_list_for_each_reverse_safe(current_node, temp_node, &group->children, link) {
        pixman_region32_translate(damage, -current_node->x, -current_node->y);
        kywc_target_offset(target, current_node->x, current_node->y);

        current_node->generate_render_task(current_node, damage, target, render_tasks);

        kywc_target_offset_revert(target, current_node->x, current_node->y);
        pixman_region32_translate(damage, current_node->x, current_node->y);
    }
}

kywc_node_generate_render_task_interface
kywc_group_node_set_generate_func(struct kywc_group_node *group,
                                  kywc_node_generate_render_task_interface generate_func)
{
    if (!group || !generate_func) {
        return NULL;
    }
    kywc_node_generate_render_task_interface default_generate = group->node.generate_render_task;
    group->node.generate_render_task = generate_func;
    return default_generate;
}

kywc_node_travel_interface kywc_group_node_set_travel_func(struct kywc_group_node *group,
                                                           kywc_node_travel_interface travel_func)
{
    if (!group || !travel_func) {
        return NULL;
    }

    kywc_node_travel_interface default_travel = group->node.travel;
    group->node.travel = travel_func;
    return default_travel;
}

/* Doesn't add damage region*/
void kywc_group_node_add(struct kywc_group_node *group, struct kywc_group_node *parent)
{
    kywc_node_add(&group->node, parent);
}

/* Doesn't add damage region*/
void kywc_group_node_init(struct kywc_group_node *group)
{
    memset(group, 0, sizeof(*group));
    kywc_node_init(&group->node);
    group->node.travel = group_node_travel;
    group->node.destroy = group_node_destroy;
    group->node.get_bounding_box = group_node_get_bounding_box;
    group->node.generate_render_task = group_node_generate_render;
    group->node.node_name = group_node_name;
    /* group->node.push_damage don't need override.*/
    group->node.get_opaque_region = group_node_get_opaque_region;
    wl_list_init(&group->children);
}

struct kywc_group_node *kywc_group_node_create(struct kywc_group_node *parent)
{
    assert(parent);

    struct kywc_group_node *group = calloc(1, sizeof(struct kywc_group_node));
    if (!group) {
        return NULL;
    }

    kywc_group_node_init(group);
    kywc_group_node_add(group, parent);
    return group;
}

/*****************************************************************************/
static const char *root_node_name(void)
{
    return "scene_root_node";
}

static struct kywc_root *root_node_from_node(struct kywc_node *node)
{
    if (!node || node->node_name != root_node_name) {
        return NULL;
    }
    struct kywc_root *root = wl_container_of(node, root, group);
    return root;
}

static void root_destroy(struct kywc_node *node)
{
    struct kywc_root *root = root_node_from_node(node);
    if (!root) {
        return;
    }

    struct kywc_scene_output *output_node, *output_tmp;
    wl_list_for_each_safe(output_node, output_tmp, &root->outputs, link) {
        kywc_scene_output_destroy(output_node);
    }

    wl_list_remove(&root->presentation_destroy.link);
    root->group_destroy(node);
}

#if 0
static void scale_output_damage(pixman_region32_t *damage, float scale) {
    wlr_region_scale(damage, damage, scale);

    if (floor(scale) != scale) {
        wlr_region_expand(damage, damage, 1);
    }
}
#endif

static void scene_damage_outputs(struct kywc_root *scene, struct wlr_box *damage)
{
    if (!damage) {
        return;
    }

    struct kywc_scene_output *scene_output;
    wl_list_for_each(scene_output, &scene->outputs, link) {
        pixman_region32_t output_damage;
        pixman_region32_init_rect(&output_damage, damage->x, damage->y, damage->width,
                                  damage->height);
        pixman_region32_translate(&output_damage, -scene_output->x, -scene_output->y);
        /* Damage use the logic coord*/
        kywc_region_adjust(&output_damage, &output_damage, 20, 20, 20, 20);
        /* Scale_output_damage(&output_damage, scene_output->output->scale); */
        if (wlr_damage_ring_add(&scene_output->damage_ring, &output_damage)) {
            wlr_output_schedule_frame(scene_output->output);
        }
        pixman_region32_fini(&output_damage);
    }
}

/* Damage must be geometry(layout or global) coordinate system in root node. */
static void root_push_damage(struct kywc_node *node, const struct wlr_box *damage)
{
    struct kywc_root *root = root_node_from_node(node);
    if (!root) {
        return;
    }

    struct wlr_box whole_local_damage = {
        .x = 0,
        .y = 0,
        .height = 0,
        .width = 0,
    };
    if (damage) {
        memcpy(&whole_local_damage, damage, sizeof(whole_local_damage));
    } else if (node->get_bounding_box) {
        node->get_bounding_box(node, &whole_local_damage);
    }
    whole_local_damage.x += node->x;
    whole_local_damage.y += node->y;

    scene_damage_outputs(root, &whole_local_damage);
}

static struct _kywc_scene_data {
    struct kywc_root *scene;
    struct wl_listener server_ready_listener;
} scene_data;

static void scene_handle_server_ready_listener(struct wl_listener *listener, void *data)
{
    _kywc_effect_add_new_view_listener();
}

static struct kywc_root *root_create(void)
{
    struct kywc_root *scene = calloc(1, sizeof(struct kywc_root));
    if (!scene) {
        return NULL;
    }

    kywc_group_node_init(&scene->group);
    kywc_group_node_add(&scene->group, NULL);
    scene->group_destroy = scene->group.node.destroy;
    scene->group.node.destroy = root_destroy;
    scene->group.node.push_damage = root_push_damage;
    /* scene->group.node.get_bounding_box don't need override. */
    scene->group.node.node_name = root_node_name;

    wl_list_init(&scene->outputs);
    wl_list_init(&scene->presentation_destroy.link);
    return scene;
}

struct kywc_root *kywc_root_create_with_server(struct server *kywc_server)
{
    _kywc_effect_init(kywc_server);
    _kywc_gl_init(kywc_server->renderer);

    scene_data.server_ready_listener.notify = scene_handle_server_ready_listener;
    wl_signal_add(&kywc_server->events.ready, &scene_data.server_ready_listener);

    scene_data.scene = root_create();
    return scene_data.scene;
}

static void scene_handle_presentation_destroy(struct wl_listener *listener, void *data)
{
    struct kywc_root *scene = wl_container_of(listener, scene, presentation_destroy);
    wl_list_remove(&scene->presentation_destroy.link);
    wl_list_init(&scene->presentation_destroy.link);
    scene->presentation = NULL;
}

void kywc_root_set_presentation(struct kywc_root *scene, struct wlr_presentation *presentation)
{
    assert(!scene->presentation);
    scene->presentation = presentation;
    scene->presentation_destroy.notify = scene_handle_presentation_destroy;
    wl_signal_add(&presentation->events.destroy, &scene->presentation_destroy);
}

/*****************************************************************************/
static void tex_node_output_update_visitor(struct kywc_node_visitor *visitor,
                                           struct kywc_texture_node *tex_node);

const struct kywc_node_visitor_interface output_update_visitor_impl = {
    .visit_node = NULL,
    .visit_group = NULL,
    .visit_rect = NULL,
    .visit_texture = tex_node_output_update_visitor,
};

struct tex_node_output_update_visitor {
    struct kywc_node_visitor base;
    struct kywc_scene_output *ignore;
    const struct wl_list *outputs;
} output_update_visitor = {
    .base.lx = 0,
    .base.ly = 0,
    .base.bypass = false,
    .base.flag = VISITOR_LEAF,
    .base.impl = &output_update_visitor_impl,
};

static void output_update_visitor_reset(struct tex_node_output_update_visitor *visitor)
{
    visitor->ignore = NULL;
    visitor->outputs = NULL;
    visitor->base.lx = 0;
    visitor->base.ly = 0;
    visitor->base.bypass = false;
    visitor->base.flag = VISITOR_LEAF;
    visitor->base.impl = &output_update_visitor_impl;
}

static void tex_node_output_update_visitor(struct kywc_node_visitor *visitor,
                                           struct kywc_texture_node *tex_node)
{
    struct tex_node_output_update_visitor *update_visitor =
        wl_container_of(visitor, update_visitor, base);

    struct wlr_box geometry_box;
    tex_node->node.get_bounding_box(&tex_node->node, &geometry_box);

    geometry_box.x += visitor->lx;
    geometry_box.y += visitor->ly;

    kywc_texture_node_update_outputs(tex_node, &geometry_box, update_visitor->outputs,
                                     update_visitor->ignore);
}

static void scene_node_output_update(struct kywc_node *node, struct wl_list *outputs,
                                     struct kywc_scene_output *ignore)
{
    output_update_visitor_reset(&output_update_visitor);
    output_update_visitor.ignore = ignore;
    output_update_visitor.outputs = outputs;
    node->travel(node, &output_update_visitor.base);
}

void node_output_update(struct kywc_node *node)
{
    output_update_visitor_reset(&output_update_visitor);

    int lx = 0, ly = 0;
    struct kywc_root *scene = kywc_node_get_root(node);
    if (node->parent) {
        kywc_node_coords(&node->parent->node, &lx, &ly);
    }

    output_update_visitor.base.lx = lx;
    output_update_visitor.base.ly = ly;
    output_update_visitor.ignore = NULL;
    output_update_visitor.outputs = &scene->outputs;

    node->travel(node, &output_update_visitor.base);
}

static void scene_output_update_geometry(struct kywc_scene_output *scene_output)
{
    int width, height;
    wlr_output_transformed_resolution(scene_output->output, &width, &height);
    wlr_damage_ring_set_bounds(&scene_output->damage_ring, width, height);
    wlr_output_schedule_frame(scene_output->output);

    scene_node_output_update(&scene_output->scene->group.node, &scene_output->scene->outputs, NULL);
}

static void scene_output_handle_commit(struct wl_listener *listener, void *data)
{
    struct kywc_scene_output *scene_output = wl_container_of(listener, scene_output, output_commit);
    struct wlr_output_event_commit *event = data;

    if (event->committed & (WLR_OUTPUT_STATE_MODE | WLR_OUTPUT_STATE_TRANSFORM |
                            WLR_OUTPUT_STATE_SCALE | WLR_OUTPUT_STATE_ENABLED)) {
        scene_output_update_geometry(scene_output);
    }
}

static void scene_output_handle_damage(struct wl_listener *listener, void *data)
{
    struct kywc_scene_output *scene_output = wl_container_of(listener, scene_output, output_damage);
    struct wlr_output_event_damage *event = data;
    if (wlr_damage_ring_add(&scene_output->damage_ring, event->damage)) {
        wlr_output_schedule_frame(scene_output->output);
    }
}

static void scene_output_handle_needs_frame(struct wl_listener *listener, void *data)
{
    struct kywc_scene_output *scene_output =
        wl_container_of(listener, scene_output, output_needs_frame);
    wlr_output_schedule_frame(scene_output->output);
}

static void scene_output_handle_destroy(struct wlr_addon *addon)
{
    struct kywc_scene_output *scene_output = wl_container_of(addon, scene_output, addon);
    kywc_scene_output_destroy(scene_output);
}

static const struct wlr_addon_interface output_addon_impl = {
    .name = "kywc_scene_output",
    .destroy = scene_output_handle_destroy,
};

/*****************************************************************************/
struct kywc_scene_output *kywc_scene_output_create(struct kywc_root *scene,
                                                   struct wlr_output *output)
{
    struct kywc_scene_output *scene_output = calloc(1, sizeof(*scene_output));
    if (!scene_output) {
        return NULL;
    }

    scene_output->output = output;
    scene_output->scene = scene;
    scene_output->effect_output = _kywc_effect_output_create(scene_output);
    wlr_addon_init(&scene_output->addon, &output->addons, scene, &output_addon_impl);

    wlr_damage_ring_init(&scene_output->damage_ring);

    int prev_output_index = -1;
    struct wl_list *prev_output_link = &scene->outputs;

    struct kywc_scene_output *current_output;
    wl_list_for_each(current_output, &scene->outputs, link) {
        if (prev_output_index + 1 != current_output->index) {
            break;
        }

        prev_output_index = current_output->index;
        prev_output_link = &current_output->link;
    }

    scene_output->index = prev_output_index + 1;
    assert(scene_output->index < 64);
    wl_list_insert(prev_output_link, &scene_output->link);

    wl_list_init(&scene_output->render_task_list);
    wl_signal_init(&scene_output->events.destroy);

    scene_output->output_commit.notify = scene_output_handle_commit;
    wl_signal_add(&output->events.commit, &scene_output->output_commit);

    scene_output->output_damage.notify = scene_output_handle_damage;
    wl_signal_add(&output->events.damage, &scene_output->output_damage);

    scene_output->output_needs_frame.notify = scene_output_handle_needs_frame;
    wl_signal_add(&output->events.needs_frame, &scene_output->output_needs_frame);

    scene_output_update_geometry(scene_output);

    return scene_output;
}

void kywc_scene_output_destroy(struct kywc_scene_output *scene_output)
{
    if (!scene_output) {
        return;
    }

    wl_signal_emit_mutable(&scene_output->events.destroy, NULL);

    _kywc_effect_output_destroy(scene_output->effect_output);

    scene_node_output_update(&scene_output->scene->group.node, &scene_output->scene->outputs,
                             scene_output);

    wlr_addon_finish(&scene_output->addon);
    wlr_damage_ring_finish(&scene_output->damage_ring);
    wl_list_remove(&scene_output->link);
    wl_list_remove(&scene_output->output_commit.link);
    wl_list_remove(&scene_output->output_damage.link);
    wl_list_remove(&scene_output->output_needs_frame.link);

    free(scene_output);
}

struct kywc_scene_output *kywc_scene_get_scene_output(struct kywc_root *scene,
                                                      struct wlr_output *output)
{
    struct wlr_addon *addon = wlr_addon_find(&output->addons, scene, &output_addon_impl);
    if (!addon) {
        return NULL;
    }
    struct kywc_scene_output *scene_output = wl_container_of(addon, scene_output, addon);
    return scene_output;
}

void kywc_scene_output_set_position(struct kywc_scene_output *scene_output, int lx, int ly)
{
    if (scene_output->x == lx && scene_output->y == ly) {
        return;
    }

    scene_output->x = lx;
    scene_output->y = ly;

    scene_output_update_geometry(scene_output);
}

static void tex_node_send_frame_done(struct kywc_node_visitor *visitor,
                                     struct kywc_texture_node *tex_node);

const struct kywc_node_visitor_interface send_frame_done_visitor_impl = {
    .visit_node = NULL,
    .visit_group = NULL,
    .visit_rect = NULL,
    .visit_texture = tex_node_send_frame_done,
};

struct tex_send_frame_done_visitor {
    struct kywc_node_visitor base;
    struct timespec *now;
    struct kywc_scene_output *current_output;
} send_frame_done_visitor = {
    .base.lx = 0,
    .base.ly = 0,
    .base.bypass = false,
    .base.flag = VISITOR_LEAF,
    .base.impl = &send_frame_done_visitor_impl,
};

static void tex_node_send_frame_done(struct kywc_node_visitor *visitor,
                                     struct kywc_texture_node *tex_node)
{
    if (visitor->impl != &send_frame_done_visitor_impl) {
        return;
    }
    struct tex_send_frame_done_visitor *done_visitor = wl_container_of(visitor, done_visitor, base);

    if (tex_node->primary_output == done_visitor->current_output) {
        kywc_texture_node_send_frame_done(tex_node, done_visitor->now);
    }
}

static void tex_send_frame_done_reset(struct tex_send_frame_done_visitor *visitor)
{
    visitor->base.lx = 0;
    visitor->base.ly = 0;
    visitor->base.bypass = false;
    visitor->base.flag = VISITOR_LEAF;
    visitor->base.impl = &send_frame_done_visitor_impl;
}

void kywc_scene_output_send_frame_done(struct kywc_scene_output *scene_output, struct timespec *now)
{
    tex_send_frame_done_reset(&send_frame_done_visitor);
    struct kywc_node *node = &scene_output->scene->group.node;
    send_frame_done_visitor.now = now;
    send_frame_done_visitor.current_output = scene_output;
    node->travel(node, &send_frame_done_visitor.base);
}

bool kywc_scene_output_commit(struct kywc_scene_output *scene_output,
                              struct kywc_effect_output *output_manager)
{
    struct wlr_output *output = scene_output->output;
    struct wlr_renderer *renderer = output->renderer;
    struct wlr_damage_ring *damage_ring = &scene_output->damage_ring;
    struct kywc_root *scene = scene_output->scene;
    struct kywc_effect_output *effect_output = scene_output->effect_output;
    int output_lx = scene_output->x;
    int output_ly = scene_output->y;
    assert(renderer);

    _kywc_effects_run(OUTPUT_EFFECT_PRE);
    _kywc_effects_run(OUTPUT_EFFECT_DAMAGE);
    int buffer_age;
    if (!wlr_output_attach_render(output, &buffer_age)) {
        return false;
    }
    /* Update current output framebuffer in gl context. */
    _kywc_gl_update_current_framebuffer();
    /* Acc damage. */
    pixman_region32_t damage;
    pixman_region32_init(&damage);
    wlr_damage_ring_get_buffer_damage(damage_ring, buffer_age, &damage);
    if (!output->needs_frame && !pixman_region32_not_empty(&damage_ring->current)) {
        pixman_region32_fini(&damage);
        wlr_output_rollback(output);
        return true;
    }
    _kywc_effects_post_update_framebuffer(effect_output);

    /* Draw scenegraph */
    struct wl_list *render_task_list = &scene_output->render_task_list;
    wl_list_init(render_task_list);
    struct kywc_render_target target;
    kywc_target_init_output_target(&target, scene_output);

    pixman_region32_t render_damage;
    pixman_region32_init(&render_damage);
    pixman_region32_copy(&render_damage, &damage);
    /* To output logic(layout coord). */
    pixman_region32_translate(&render_damage, output_lx, output_ly);
    scene->group.node.generate_render_task(&scene->group.node, &render_damage, &target,
                                           render_task_list);

    pixman_region32_copy(&render_damage, &damage);
    /* To output logic(layout coord). */
    pixman_region32_translate(&render_damage, output_lx, output_ly);
    kywc_target_logic_damage_clear(&target, &render_damage);
    pixman_region32_fini(&render_damage);

    struct kywc_render_task *task, *temp_task;
    /* Last in, first out (LIFO) */
    wl_list_for_each_safe(task, temp_task, render_task_list, link) {
        /* Last in, last out (LILO) */
        // wl_list_for_each_reverse_safe(task, temp_task, render_task_list, link) {
        task->instance->impl->render(task->instance, task->target, &task->damage);
        wl_list_remove(&task->link);
        kywc_render_task_release(task);
    }

    _kywc_effects_run(OUTPUT_EFFECT_OVERLAY);
    _kywc_effects_run_post(effect_output);
    struct wlr_render_pass *pass =
        wlr_renderer_begin_buffer_pass(renderer, output->back_buffer, NULL);
    wlr_output_add_software_cursors_to_render_pass(output, pass, &damage);
    wlr_render_pass_submit(pass);
    pixman_region32_fini(&damage);

    int tr_width, tr_height;
    wlr_output_transformed_resolution(output, &tr_width, &tr_height);

    enum wl_output_transform transform = wlr_output_transform_invert(output->transform);

    pixman_region32_t frame_damage;
    pixman_region32_init(&frame_damage);
    wlr_region_transform(&frame_damage, &damage_ring->current, transform, tr_width, tr_height);
    wlr_output_set_damage(output, &frame_damage);
    pixman_region32_fini(&frame_damage);

    bool success = wlr_output_commit(output);
    if (success) {
        wlr_damage_ring_rotate(damage_ring);
    }
    _kywc_effects_run(OUTPUT_EFFECT_POST);
    return success;
}

static void pimxman_box_scale_xy(const pixman_box32_t *src_rect, pixman_box32_t *dst_rect,
                                 float scale_x, float scale_y)
{
    struct wlr_box dst;
    struct wlr_box src = {
        .x = src_rect->x1,
        .y = src_rect->y1,
        .width = src_rect->x2 - src_rect->x1,
        .height = src_rect->y2 - src_rect->y1,
    };
    kywc_box_scale_xy(&src, &dst, scale_x, scale_y);

    dst_rect->x1 = dst.x;
    dst_rect->y1 = dst.y;
    dst_rect->x2 = dst.x + dst.width;
    dst_rect->y2 = dst.y + dst.height;
}

void kywc_box_scale_xy(const struct wlr_box *src, struct wlr_box *dst, float scale_x, float scale_y)
{
    dst->x = floor(src->x * scale_x);
    dst->y = floor(src->y * scale_y);
    dst->width = ceil((src->x + src->width) * scale_x) - dst->x;
    dst->height = ceil((src->y + src->height) * scale_y) - dst->y;
}

void kywc_region_adjust(pixman_region32_t *dst, const pixman_region32_t *src, int top, int left,
                        int bottom, int right)
{
    if (top == 0 && left == 0 && bottom == 0 && right == 0) {
        pixman_region32_copy(dst, src);
        return;
    }

    int nrects;
    const pixman_box32_t *src_rects = pixman_region32_rectangles(src, &nrects);

    pixman_box32_t *dst_rects = malloc(nrects * sizeof(pixman_box32_t));
    if (!dst_rects) {
        return;
    }

    for (int i = 0; i < nrects; ++i) {
        dst_rects[i].x1 = src_rects[i].x1 - top;
        dst_rects[i].x2 = src_rects[i].x2 + bottom;
        dst_rects[i].y1 = src_rects[i].y1 - left;
        dst_rects[i].y2 = src_rects[i].y2 + right;
    }

    pixman_region32_fini(dst);
    pixman_region32_init_rects(dst, dst_rects, nrects);
    free(dst_rects);
}

void kywc_region_scale_xy(pixman_region32_t *dst, const pixman_region32_t *src, float scale_x,
                          float scale_y)
{
    if (scale_x == 1.0 && scale_y == 1.0) {
        pixman_region32_copy(dst, src);
        return;
    }

    int nrects;
    const pixman_box32_t *src_rects = pixman_region32_rectangles(src, &nrects);

    pixman_box32_t *dst_rects = malloc(nrects * sizeof(pixman_box32_t));
    if (dst_rects == NULL) {
        return;
    }

    for (int i = 0; i < nrects; ++i) {
        pimxman_box_scale_xy(&src_rects[i], &dst_rects[i], scale_x, scale_y);
    }

    pixman_region32_fini(dst);
    pixman_region32_init_rects(dst, dst_rects, nrects);
    free(dst_rects);
}

void kywc_region_scale(pixman_region32_t *dst, const pixman_region32_t *src, float scale)
{
    kywc_region_scale_xy(dst, src, scale, scale);
}

void kywc_scene_node_render(struct kywc_node *node, struct kywc_render_target *target,
                            pixman_region32_t *damage)
{
    struct wl_list render_task_list;
    wl_list_init(&render_task_list);

    node->generate_render_task(node, damage, target, &render_task_list);
    struct kywc_render_task *task, *temp_task;
    wl_list_for_each_safe(task, temp_task, &render_task_list, link) {
        task->instance->impl->render(task->instance, task->target, &task->damage);
        wl_list_remove(&task->link);
        kywc_render_task_release(task);
    }
}
