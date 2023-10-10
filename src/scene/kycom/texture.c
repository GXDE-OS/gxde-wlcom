// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>

#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>

#include "kywc/kycom/texture.h"

struct texture_node_render_instance {
    struct kywc_render_instance instance_base;
    struct kywc_texture_node *texture;
};

void node_output_update(struct kywc_node *node);

/********************texture_render_instance*****************************/
static void gl_texture_render(struct kywc_render_instance *instance,
                              struct kywc_render_target *target, pixman_region32_t *damage);

const struct kywc_render_instance_interface texture_node_render_instance_impl = {
    .render = gl_texture_render,
    .compute_visible = NULL,
    .try_direct_scanout = NULL,
};

static void gl_texture_render(struct kywc_render_instance *instance,
                              struct kywc_render_target *target, pixman_region32_t *damage)
{
    struct texture_node_render_instance *render = wl_container_of(instance, render, instance_base);
    if (!render->texture) {
        return;
    }
    struct kywc_gl_texture *tex = render->texture->texture;
    if (!tex) {
        return;
    }

    struct kywc_gl_geometry geometry = {
        .x1 = 0,
        .y1 = 0,
        .x2 = render->texture->geometry_width,
        .y2 = render->texture->geometry_height,
    };

    struct wlr_fbox src_box = render->texture->src_box;
    kywc_gl_texture_update_src_box(tex, &src_box);
    kywc_target_render_begin(target);
    vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    kywc_target_render_texture_with_transform(tex,
                                              (enum kywc_gl_transform)render->texture->transform,
                                              target, &geometry, color, RENDER_FLAG_CACHED);

    kywc_target_draw_damage(target, tex, damage);

    kywc_gl_render_clear_cached();

    kywc_target_render_end(target);
}

static struct texture_node_render_instance *
texture_node_render_instance_create(struct kywc_texture_node *texture_node)
{
    struct texture_node_render_instance *texture_render = malloc(sizeof(*texture_render));
    if (!texture_render) {
        return NULL;
    }

    texture_render->texture = texture_node;

    kywc_render_instance_init(&texture_render->instance_base, &texture_node_render_instance_impl,
                              kywc_render_instance_handle_destroy);

    return texture_render;
}

/********************generate_render_task*******************************/
void kywc_texture_node_generate_render_task(const struct kywc_node *node, pixman_region32_t *damage,
                                            struct kywc_render_target *target,
                                            struct wl_list *render_tasks)
{
    struct kywc_texture_node *texture_node = wl_container_of(node, texture_node, node);
    if (!render_tasks || !damage || !node->enabled) {
        return;
    }

    /* local logic damage. */
    pixman_region32_t node_damage, node_damage_opaque;
    pixman_region32_init(&node_damage_opaque);
    pixman_region32_init(&node_damage);
    pixman_region32_intersect_rect(&node_damage, damage, 0, 0, texture_node->geometry_width,
                                   texture_node->geometry_height);

    if (!pixman_region32_not_empty(&node_damage)) {
        goto final;
        return;
    }

    pixman_region32_intersect(&node_damage_opaque, &node_damage, &texture_node->opaque_region);
    pixman_region32_subtract(damage, damage, &node_damage_opaque);

    struct texture_node_render_instance *texture_render =
        texture_node_render_instance_create(texture_node);

    struct kywc_render_task *task =
        kywc_render_task_create(&texture_render->instance_base, &node_damage, target);

    wl_list_insert(render_tasks, &task->link);

final:
    pixman_region32_fini(&node_damage_opaque);
    pixman_region32_fini(&node_damage);
}

/********************kywc_texture_node************************************/
static void texture_node_get_bounding_box(const struct kywc_node *node, struct wlr_box *box)
{
    if (!box) {
        return;
    }
    struct kywc_texture_node *texture_node = kywc_texture_node_from_node((struct kywc_node *)node);
    if (!texture_node || !node->enabled) {
        memset(box, 0, sizeof(*box));
        return;
    }

    box->x = 0;
    box->y = 0;
    box->width = texture_node->geometry_width;
    box->height = texture_node->geometry_height;
}

static void texture_node_destroy(struct kywc_node *node)
{
    struct kywc_texture_node *texture_node = kywc_texture_node_from_node(node);
    if (!texture_node) {
        return;
    }
    struct kywc_root *scene = kywc_node_get_root(node);
    uint64_t active = texture_node->active_outputs;
    if (active) {
        struct kywc_scene_output *scene_output;
        wl_list_for_each(scene_output, &scene->outputs, link) {
            if (active & (1ull << scene_output->index)) {
                wl_signal_emit_mutable(&texture_node->events.output_leave, scene_output);
            }
        }
    }
    if (texture_node->buffer) {
        wlr_buffer_unlock(texture_node->buffer);
        kywc_gl_texture_destroy(texture_node->texture);
    }

    pixman_region32_fini(&texture_node->opaque_region);
    pixman_region32_fini(&texture_node->corner_region);

    if (texture_node->base_node_destroy) {
        texture_node->base_node_destroy(node);
    }
}

static void texture_node_travel(struct kywc_node *node, struct kywc_node_visitor *visitor)
{
    if (!node) {
        return;
    }

    struct kywc_texture_node *texture_node = kywc_texture_node_from_node(node);

    if (!visitor || !visitor->impl || !visitor->impl->visit_texture || !texture_node ||
        visitor->travel_break) {
        return;
    }
    visitor->flag = VISITOR_LEAF;
    visitor->lx += node->x;
    visitor->ly += node->y;

    visitor->impl->visit_texture(visitor, texture_node);
    visitor->skip_current_node = false;

    visitor->lx -= node->x;
    visitor->ly -= node->y;
}

static void texture_node_get_opaque_region(const struct kywc_node *node,
                                           pixman_region32_t *opaque_region)
{
    struct kywc_texture_node *texture_node = kywc_texture_node_from_node((struct kywc_node *)node);
    if (!texture_node || !opaque_region) {
        return;
    }

    pixman_region32_copy(opaque_region, &texture_node->opaque_region);
}

static const char *texture_node_name(void)
{
    return "texture_node";
}

struct kywc_texture_node *kywc_texture_node_from_node(struct kywc_node *node)
{
    if (!node || node->node_name != texture_node_name) {
        return NULL;
    }
    struct kywc_texture_node *texture_node = wl_container_of(node, texture_node, node);
    return texture_node;
}

struct kywc_texture_node *kywc_texture_node_create(struct kywc_group_node *parent,
                                                   struct kywc_gl_texture *texture)
{
    struct kywc_texture_node *texture_node = malloc(sizeof(*texture_node));
    if (!texture_node) {
        return NULL;
    }
    memset(texture_node, 0, sizeof(*texture_node));

    wl_signal_init(&texture_node->events.outputs_update);
    wl_signal_init(&texture_node->events.output_enter);
    wl_signal_init(&texture_node->events.output_leave);
    wl_signal_init(&texture_node->events.output_sample);
    wl_signal_init(&texture_node->events.output_present);
    wl_signal_init(&texture_node->events.output_sample);
    wl_signal_init(&texture_node->events.frame_done);
    pixman_region32_init(&texture_node->opaque_region);
    pixman_region32_init(&texture_node->corner_region);

    texture_node->texture = texture;
    texture_node->buffer = NULL;
    kywc_node_init(&texture_node->node);
    kywc_node_add(&texture_node->node, parent);

    texture_node->node.generate_render_task = kywc_texture_node_generate_render_task;
    texture_node->node.get_bounding_box = texture_node_get_bounding_box;
    texture_node->node.travel = texture_node_travel;
    texture_node->base_node_destroy = texture_node->node.destroy;
    texture_node->node.destroy = texture_node_destroy;
    texture_node->node.get_opaque_region = texture_node_get_opaque_region;
    /* texture_node->node.push_damage don't need override。 */
    texture_node->node.node_name = texture_node_name;
    /* Update damage region. */
    texture_node->node.push_damage(&texture_node->node, NULL);
    return texture_node;
}

struct kywc_texture_node *kywc_texture_node_create_from_buffer(struct kywc_group_node *parent,
                                                               struct wlr_buffer *buffer)
{
    struct kywc_gl_texture *texture = buffer ? kywc_gl_texture_from_buffer(buffer) : NULL;
    struct kywc_texture_node *tex_node = kywc_texture_node_create(parent, texture);
    tex_node->buffer = buffer ? wlr_buffer_lock(buffer) : NULL;
    return tex_node;
}

void kywc_texture_node_set_buffer_with_damage(struct kywc_texture_node *tex_node,
                                              struct wlr_buffer *buffer,
                                              const pixman_region32_t *damage)
{
    assert(buffer || !damage);

    struct kywc_gl_texture *tex = kywc_gl_texture_from_buffer(buffer);
    if (buffer && !tex) {
        wlr_log(WLR_ERROR, "Texture_node: Can't get texture from buffer, set buffer failed!");
        return;
    }
    /* Texture from buffer. */
    struct kywc_gl_texture *tex_should_destroy = NULL;
    if (tex_node->buffer) {
        wlr_buffer_unlock(tex_node->buffer);
        /* tex_node->texture update in set_texture_with_damage. */
        tex_should_destroy = tex_node->texture;
    }

    tex_node->buffer = buffer ? wlr_buffer_lock(buffer) : NULL;

    kywc_texture_node_set_texture_with_damage(tex_node, tex, damage);

    if (tex_should_destroy) {
        kywc_gl_texture_destroy(tex_should_destroy);
    }
}

static void push_scaled_damage_region(pixman_region32_t *wl_buffer_damage,
                                      struct kywc_texture_node *node, float scale_x, float scale_y)
{
    int nrects;
    const pixman_box32_t *src_rects = pixman_region32_rectangles(wl_buffer_damage, &nrects);
    struct wlr_box buffer_box, buffer_logic_box;
    for (int i = 0; i < nrects; ++i) {
        buffer_box.x = src_rects[i].x1;
        buffer_box.y = src_rects[i].y1;
        buffer_box.width = src_rects[i].x2 - src_rects[i].x1;
        buffer_box.height = src_rects[i].y2 - src_rects[i].y1;

        kywc_box_scale_xy(&buffer_box, &buffer_logic_box, scale_x, scale_y);
        node->node.push_damage(&node->node, &buffer_logic_box);
    }
}

void kywc_texture_node_set_texture_with_damage(struct kywc_texture_node *tex_node,
                                               struct kywc_gl_texture *texture,
                                               const pixman_region32_t *damage)
{
    assert(tex_node || !damage);
    if (!tex_node) {
        return;
    }

    bool update_output = false;
    if (texture) {
        if (!tex_node->texture) {
            update_output = true;
        } else if (tex_node->texture->width == texture->width &&
                   tex_node->texture->height == texture->height) {
            update_output = false;
        }
        tex_node->texture = texture;
    } else {
        if (!tex_node->texture) {
            return;
        }
        /* Set texture node's texture to NULL. */
        texture = tex_node->texture;
        tex_node->texture = NULL;
        update_output = true;
    }

    int lx, ly;
    bool enabled = kywc_node_coords(&tex_node->node, &lx, &ly);
    struct kywc_root *scene = kywc_node_get_root(&tex_node->node);
    if (update_output) {
        struct wlr_box geometry_box = {
            .x = lx,
            .y = ly,
            .width = tex_node->geometry_width,
            .height = tex_node->geometry_height,
        };
        kywc_texture_node_update_outputs(tex_node, &geometry_box, &scene->outputs, NULL);
    }

    if (!enabled) {
        /* Don't push damage. */
        return;
    }

    pixman_region32_t fallback_damage;
    pixman_region32_init_rect(&fallback_damage, 0, 0, texture->width, texture->height);
    if (!damage) {
        damage = &fallback_damage;
    }
    struct wlr_fbox box;
    if (wlr_fbox_empty(&tex_node->src_box)) {
        box.x = 0;
        box.y = 0;
        box.width = tex_node->geometry_width;
        box.height = tex_node->geometry_height;
    } else {
        memcpy(&box, &tex_node->src_box, sizeof(tex_node->src_box));
    }
    /* src_box */
    wlr_fbox_transform(&box, &box, tex_node->transform, texture->width, texture->height);

    /* Scale of texture(buffer) to (texture_node)surface. */
    float scale_x, scale_y;
    if (tex_node->geometry_height || tex_node->geometry_width) {
        scale_x = tex_node->geometry_width / box.width;
        scale_y = tex_node->geometry_height / box.height;
    } else {
        scale_x = texture->width / box.width;
        scale_y = texture->height / box.height;
    }

    /**
     * (Adjust the coordinate system of the damage from the texture(buffer)
     * coordinate system to the texture(surface) coordinate system.
     */
    pixman_region32_t trans_damage;
    pixman_region32_init(&trans_damage);
    /* Damage rotate */
    wlr_region_transform(&trans_damage, damage, tex_node->transform, tex_node->geometry_width,
                         tex_node->geometry_height);
    pixman_region32_intersect_rect(&trans_damage, &trans_damage, box.x, box.y, box.width,
                                   box.height);

    /* Damage scale(damage in surface) */
    push_scaled_damage_region(&trans_damage, tex_node, scale_x, scale_y);
    pixman_region32_fini(&trans_damage);
    pixman_region32_fini(&fallback_damage);
    return;
}

void kywc_texture_node_set_buffer(struct kywc_texture_node *tex_node, struct wlr_buffer *buffer)
{
    kywc_texture_node_set_buffer_with_damage(tex_node, buffer, NULL);
}

void kywc_texture_node_set_texture(struct kywc_texture_node *tex_node,
                                   struct kywc_gl_texture *texture)
{
    kywc_texture_node_set_texture_with_damage(tex_node, texture, NULL);
}

void kywc_texture_node_set_opaque_region(struct kywc_texture_node *tex_node,
                                         const pixman_region32_t *region)
{
    if (pixman_region32_equal(&tex_node->opaque_region, region)) {
        return;
    }

    tex_node->node.push_damage(&tex_node->node, NULL);
    pixman_region32_copy(&tex_node->opaque_region, region);
    tex_node->node.push_damage(&tex_node->node, NULL);
}

void kywc_texture_node_set_corner_region(struct kywc_texture_node *tex_node,
                                         const pixman_region32_t *region)
{
    if (pixman_region32_equal(&tex_node->corner_region, region)) {
        return;
    }

    pixman_region32_copy(&tex_node->corner_region, region);
}

void kywc_texture_node_set_source_box(struct kywc_texture_node *tex_node,
                                      const struct wlr_fbox *box)
{
    struct wlr_fbox *cur = &tex_node->src_box;
    if ((wlr_fbox_empty(box) && wlr_fbox_empty(cur)) || (box && wlr_fbox_equal(cur, box))) {
        return;
    }

    if (box) {
        memcpy(cur, box, sizeof(*box));
    } else {
        memset(cur, 0, sizeof(*cur));
    }

    tex_node->node.push_damage(&tex_node->node, NULL);
}

void kywc_texture_node_set_size(struct kywc_texture_node *node, int width, int height)
{
    if (!node) {
        return;
    }

    if (node->geometry_width == width && node->geometry_height == height) {
        return;
    }
    bool bafter_update = false;
    if (node->geometry_width > width || node->geometry_height > height) {
        node->node.push_damage(&node->node, NULL);
    } else if (node->geometry_width < width || node->geometry_height < height) {
        bafter_update = true;
    }

    node->geometry_width = width;
    node->geometry_height = height;
    if (bafter_update) {
        node->node.push_damage(&node->node, NULL);
    }
    node_output_update(&node->node);
}

void kywc_texture_node_set_transform(struct kywc_texture_node *tex_node,
                                     enum wl_output_transform transform)
{
    if (tex_node->transform == transform) {
        return;
    }

    tex_node->node.push_damage(&tex_node->node, NULL);
    tex_node->transform = transform;
    tex_node->node.push_damage(&tex_node->node, NULL);
}

void kywc_texture_node_send_frame_done(struct kywc_texture_node *tex_node, struct timespec *now)
{
    wl_signal_emit_mutable(&tex_node->events.frame_done, now);
}

static uint32_t region_area(pixman_region32_t *region)
{
    uint32_t area = 0;

    int nrects;
    pixman_box32_t *rects = pixman_region32_rectangles(region, &nrects);
    for (int i = 0; i < nrects; ++i) {
        area += (rects[i].x2 - rects[i].x1) * (rects[i].y2 - rects[i].y1);
    }

    return area;
}

void kywc_texture_node_update_outputs(struct kywc_texture_node *tex_node,
                                      struct wlr_box *geometry_box, const struct wl_list *outputs,
                                      struct kywc_scene_output *ignore)
{
    if (!tex_node) {
        return;
    }

    uint32_t largest_overlap = 0;
    tex_node->primary_output = NULL;

    size_t count = 0;
    uint64_t active_outputs = 0;

    /**
     * Let's update the outputs in two steps:
     * the primary outputs
     * the enter/leave signals
     * This ensures that the enter/leave signals can rely on the primary output
     * to have a reasonable value. Otherwise, they may get a value that's in
     * the middle of a calculation.
     */
    struct kywc_scene_output *scene_output;
    wl_list_for_each(scene_output, outputs, link) {
        if (scene_output == ignore) {
            continue;
        }

        if (!scene_output->output->enabled) {
            continue;
        }

        struct wlr_box output_box = {
            .x = scene_output->x,
            .y = scene_output->y,
        };
        wlr_output_effective_resolution(scene_output->output, &output_box.width,
                                        &output_box.height);

        pixman_region32_t intersection, box_region;
        pixman_region32_init(&intersection);
        pixman_region32_init_rect(&box_region, geometry_box->x, geometry_box->y,
                                  geometry_box->width, geometry_box->height);
        pixman_region32_intersect_rect(&intersection, &box_region, output_box.x, output_box.y,
                                       output_box.width, output_box.height);

        if (pixman_region32_not_empty(&intersection)) {
            uint32_t overlap = region_area(&intersection);
            if (overlap >= largest_overlap) {
                largest_overlap = overlap;
                tex_node->primary_output = scene_output;
            }

            active_outputs |= 1ull << scene_output->index;
            count++;
        }

        pixman_region32_fini(&intersection);
        pixman_region32_fini(&box_region);
    }

    uint64_t old_active = tex_node->active_outputs;
    tex_node->active_outputs = active_outputs;
    wl_list_for_each(scene_output, outputs, link) {
        uint64_t mask = 1ull << scene_output->index;
        bool intersects = active_outputs & mask;
        bool intersects_before = old_active & mask;
        if (intersects && !intersects_before) {
            wl_signal_emit_mutable(&tex_node->events.output_enter, scene_output);
        } else if (!intersects && intersects_before) {
            wl_signal_emit_mutable(&tex_node->events.output_leave, scene_output);
        }
    }

    /**
     * If there are active outputs on this node, we should always have a primary
     * output.
     */
    assert(!tex_node->active_outputs || tex_node->primary_output);

    /* If no outputs changes intersection status, skip calling outputs_update. */
    if (old_active == active_outputs) {
        return;
    }

    struct wl_array active_outputs_array;
    wl_array_init(&active_outputs_array);

    size_t i = 0;
    wl_list_for_each(scene_output, outputs, link) {
        if (~active_outputs & (1ull << scene_output->index)) {
            continue;
        }

        assert(i < count);
        struct kywc_scene_output **array_entry =
            wl_array_add(&active_outputs_array, sizeof(scene_output));
        if (array_entry) {
            *array_entry = scene_output;
        }
    }

    wl_signal_emit_mutable(&tex_node->events.outputs_update, &active_outputs_array);

    wl_array_release(&active_outputs_array);
}
