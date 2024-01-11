// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>

#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>

#include "scene_p.h"

struct ky_scene_buffer *ky_scene_buffer_from_node(struct ky_scene_node *node)
{
    struct ky_scene_buffer *scene_buffer = wl_container_of(node, scene_buffer, node);
    return scene_buffer;
}

static void buffer_get_dest_size(struct ky_scene_buffer *scene_buffer, int *width, int *height)
{
    if (scene_buffer->dst_width > 0 && scene_buffer->dst_height > 0) {
        *width = scene_buffer->dst_width;
        *height = scene_buffer->dst_height;
    } else if (scene_buffer->buffer) {
        if (scene_buffer->transform & WL_OUTPUT_TRANSFORM_90) {
            *height = scene_buffer->buffer->width;
            *width = scene_buffer->buffer->height;
        } else {
            *width = scene_buffer->buffer->width;
            *height = scene_buffer->buffer->height;
        }
    } else {
        *width = *height = 0;
    }
}

static struct ky_scene_node *buffer_accpet_input(struct ky_scene_node *node, int lx, int ly,
                                                 double px, double py, double *rx, double *ry)
{
    /* skip disabled or input bypassed nodes */
    if (!node->enabled || node->bypassed) {
        return NULL;
    }

    struct ky_scene_buffer *scene_buffer = ky_scene_buffer_from_node(node);
    struct wlr_box box = { floor(px), floor(py), 1, 1 };
    struct wlr_box node_box = { .x = lx, .y = ly };
    buffer_get_dest_size(scene_buffer, &node_box.width, &node_box.height);

    if (!wlr_box_intersection(&node_box, &node_box, &box)) {
        return NULL;
    }

    *rx = px - lx;
    *ry = py - ly;

    /* check buffer_point_accepts_input */
    if (scene_buffer->point_accepts_input &&
        !scene_buffer->point_accepts_input(scene_buffer, rx, ry)) {
        return NULL;
    }

    return node;
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

static void buffer_update_outputs(struct ky_scene_node *node, int lx, int ly,
                                  struct wl_list *outputs, struct ky_scene_output *ignore,
                                  struct ky_scene_output *force)
{
    struct ky_scene_buffer *scene_buffer = ky_scene_buffer_from_node(node);

    uint32_t largest_overlap = 0;
    struct ky_scene_output *old_primary_output = scene_buffer->primary_output;
    scene_buffer->primary_output = NULL;

    size_t count = 0;
    uint64_t active_outputs = 0;
    int width, height;

    // let's update the outputs in two steps:
    //  - the primary outputs
    //  - the enter/leave signals
    // This ensures that the enter/leave signals can rely on the primary output
    // to have a reasonable value. Otherwise, they may get a value that's in
    // the middle of a calculation.
    struct ky_scene_output *scene_output;
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

        pixman_region32_t intersection, node_region;
        pixman_region32_init(&intersection);
        buffer_get_dest_size(scene_buffer, &width, &height);
        pixman_region32_init_rect(&node_region, lx, ly, width, height);
        pixman_region32_intersect_rect(&intersection, &node_region, output_box.x, output_box.y,
                                       output_box.width, output_box.height);

        if (pixman_region32_not_empty(&intersection)) {
            uint32_t overlap = region_area(&intersection);
            if (overlap >= largest_overlap) {
                largest_overlap = overlap;
                scene_buffer->primary_output = scene_output;
            }

            active_outputs |= 1ull << scene_output->index;
            count++;
        }

        pixman_region32_fini(&intersection);
        pixman_region32_fini(&node_region);
    }

    uint64_t old_active = scene_buffer->active_outputs;
    scene_buffer->active_outputs = active_outputs;

    wl_list_for_each(scene_output, outputs, link) {
        uint64_t mask = 1ull << scene_output->index;
        bool intersects = active_outputs & mask;
        bool intersects_before = old_active & mask;

        if (intersects && !intersects_before) {
            wl_signal_emit_mutable(&scene_buffer->events.output_enter, scene_output);
        } else if (!intersects && intersects_before) {
            wl_signal_emit_mutable(&scene_buffer->events.output_leave, scene_output);
        }
    }

    // if there are active outputs on this node, we should always have a primary output
    assert(!scene_buffer->active_outputs || scene_buffer->primary_output);

    // Skip output update event if nothing was updated
    if (old_active == active_outputs && (!force || ((1ull << force->index) & ~active_outputs)) &&
        old_primary_output == scene_buffer->primary_output) {
        return;
    }

    struct ky_scene_output *outputs_array[64];
    struct ky_scene_outputs_update_event event = {
        .active = outputs_array,
        .size = count,
    };

    size_t i = 0;
    wl_list_for_each(scene_output, outputs, link) {
        if (~active_outputs & (1ull << scene_output->index)) {
            continue;
        }

        assert(i < count);
        outputs_array[i++] = scene_output;
    }

    wl_signal_emit_mutable(&scene_buffer->events.outputs_update, &event);
}

static struct wlr_texture *scene_buffer_get_texture(struct ky_scene_buffer *scene_buffer,
                                                    struct wlr_renderer *renderer)
{
    struct wlr_client_buffer *client_buffer = wlr_client_buffer_get(scene_buffer->buffer);
    if (client_buffer != NULL) {
        return client_buffer->texture;
    }

    if (scene_buffer->texture != NULL) {
        return scene_buffer->texture;
    }

    scene_buffer->texture = wlr_texture_from_buffer(renderer, scene_buffer->buffer);
    return scene_buffer->texture;
}

static void buffer_render(struct ky_scene_node *node, int lx, int ly,
                          struct ky_scene_render_target *target)

{
    if (!node->enabled) {
        return;
    }

    struct ky_scene_buffer *scene_buffer = ky_scene_buffer_from_node(node);
    if (scene_buffer->opacity == 0) {
        return;
    }

    int width, height;
    buffer_get_dest_size(scene_buffer, &width, &height);

    struct wlr_box dst_box = {
        .x = lx - target->logical.x,
        .y = ly - target->logical.y,
        .width = width,
        .height = height,
    };

    struct wlr_box clip_box;
    if (!ky_scene_render_box(&clip_box, &dst_box, target)) {
        return;
    }

    struct wlr_texture *texture =
        scene_buffer_get_texture(scene_buffer, target->output->output->renderer);
    if (texture == NULL) {
        return;
    }

    pixman_region32_t render_region;
    pixman_region32_init_rect(&render_region, clip_box.x, clip_box.y, clip_box.width,
                              clip_box.height);
    ky_scene_render_region(&render_region, target);

    enum wl_output_transform transform = wlr_output_transform_invert(scene_buffer->transform);
    transform = wlr_output_transform_compose(transform, target->transform);

    wlr_render_pass_add_texture(target->render_pass, &(struct wlr_render_texture_options){
                                                         .texture = texture,
                                                         .src_box = scene_buffer->src_box,
                                                         .dst_box = dst_box,
                                                         .transform = transform,
                                                         .alpha = &scene_buffer->opacity,
                                                         .clip = &render_region,
                                                     });

    pixman_region32_fini(&render_region);

    struct ky_scene_output_sample_event sample_event = {
        .output = target->output,
        .direct_scanout = false,
    };
    wl_signal_emit_mutable(&scene_buffer->events.output_sample, &sample_event);
}

static void buffer_destroy(struct ky_scene_node *node)
{
    if (!node) {
        return;
    }

    struct ky_scene_buffer *scene_buffer = ky_scene_buffer_from_node(node);
    struct ky_scene *scene = ky_scene_from_node(node);

    uint64_t active = scene_buffer->active_outputs;
    if (active) {
        struct ky_scene_output *scene_output;
        wl_list_for_each(scene_output, &scene->outputs, link) {
            if (active & (1ull << scene_output->index)) {
                wl_signal_emit_mutable(&scene_buffer->events.output_leave, scene_output);
            }
        }
    }

    if (scene_buffer->buffer) {
        wlr_buffer_unlock(scene_buffer->buffer);
        wlr_texture_destroy(scene_buffer->texture);
    }

    pixman_region32_fini(&scene_buffer->opaque_region);
    scene_buffer->node_destroy(node);
}

static void scene_buffer_init(struct ky_scene_buffer *scene_buffer, struct ky_scene_tree *parent)
{
    *scene_buffer = (struct ky_scene_buffer){
        .opacity = 1,
    };
    ky_scene_node_init(&scene_buffer->node, parent);

    scene_buffer->node.type = KY_SCENE_NODE_BUFFER;

    scene_buffer->node_destroy = scene_buffer->node.impl.destroy;
    scene_buffer->node.impl.destroy = buffer_destroy;

    scene_buffer->node.impl.accpet_input = buffer_accpet_input;
    scene_buffer->node.impl.update_outputs = buffer_update_outputs;
    scene_buffer->node.impl.render = buffer_render;

    wl_signal_init(&scene_buffer->events.outputs_update);
    wl_signal_init(&scene_buffer->events.output_enter);
    wl_signal_init(&scene_buffer->events.output_leave);
    wl_signal_init(&scene_buffer->events.output_sample);
    wl_signal_init(&scene_buffer->events.frame_done);

    pixman_region32_init(&scene_buffer->opaque_region);
}

struct ky_scene_buffer *ky_scene_buffer_create(struct ky_scene_tree *parent,
                                               struct wlr_buffer *buffer)
{
    struct ky_scene_buffer *scene_buffer = calloc(1, sizeof(*scene_buffer));
    if (!scene_buffer) {
        return NULL;
    }

    scene_buffer_init(scene_buffer, parent);

    if (buffer) {
        scene_buffer->buffer = wlr_buffer_lock(buffer);
        ky_scene_node_update_outputs(&scene_buffer->node, NULL, NULL, NULL);
    }

    return scene_buffer;
}

void ky_scene_buffer_set_buffer_with_damage(struct ky_scene_buffer *scene_buffer,
                                            struct wlr_buffer *buffer,
                                            const pixman_region32_t *damage)
{
    assert(buffer || !damage);
    int old_width, old_height, new_width, new_height;

    wlr_texture_destroy(scene_buffer->texture);
    scene_buffer->texture = NULL;

    buffer_get_dest_size(scene_buffer, &old_width, &old_height);
    wlr_buffer_unlock(scene_buffer->buffer);
    scene_buffer->buffer = buffer ? wlr_buffer_lock(buffer) : NULL;
    buffer_get_dest_size(scene_buffer, &new_width, &new_height);

    /* return early if the scene buffer output no need to update */
    if (old_width == new_width && old_height == new_height) {
        return;
    }

    // buffer update outputs, leave active outputs when no buffer
    ky_scene_node_update_outputs(&scene_buffer->node, NULL, NULL, NULL);
}

void ky_scene_buffer_set_buffer(struct ky_scene_buffer *scene_buffer, struct wlr_buffer *buffer)
{
    ky_scene_buffer_set_buffer_with_damage(scene_buffer, buffer, NULL);
}

void ky_scene_buffer_set_opaque_region(struct ky_scene_buffer *scene_buffer,
                                       const pixman_region32_t *region)
{
    if (pixman_region32_equal(&scene_buffer->opaque_region, region)) {
        return;
    }

    pixman_region32_copy(&scene_buffer->opaque_region, region);
}

void ky_scene_buffer_set_source_box(struct ky_scene_buffer *scene_buffer,
                                    const struct wlr_fbox *box)
{
    if (wlr_fbox_equal(&scene_buffer->src_box, box)) {
        return;
    }

    if (box != NULL) {
        scene_buffer->src_box = *box;
    } else {
        scene_buffer->src_box = (struct wlr_fbox){ 0 };
    }
}

void ky_scene_buffer_set_dest_size(struct ky_scene_buffer *scene_buffer, int width, int height)
{
    if (scene_buffer->dst_width == width && scene_buffer->dst_height == height) {
        return;
    }

    scene_buffer->dst_width = width;
    scene_buffer->dst_height = height;

    ky_scene_node_update_outputs(&scene_buffer->node, NULL, NULL, NULL);
}

void ky_scene_buffer_set_transform(struct ky_scene_buffer *scene_buffer,
                                   enum wl_output_transform transform)
{
    if (scene_buffer->transform == transform) {
        return;
    }

    scene_buffer->transform = transform;
}

void ky_scene_buffer_set_opacity(struct ky_scene_buffer *scene_buffer, float opacity)
{
    assert(opacity != 0);
    if (scene_buffer->opacity == opacity) {
        return;
    }

    scene_buffer->opacity = opacity;
}

void ky_scene_node_update_outputs(struct ky_scene_node *node, struct wl_list *outputs,
                                  struct ky_scene_output *ignore, struct ky_scene_output *force)
{
    if (!outputs) {
        outputs = &ky_scene_from_node(node)->outputs;
    }

    int x, y;
    ky_scene_node_coords(node, &x, &y);

    node->impl.update_outputs(node, x, y, outputs, ignore, force);
}
