// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>

#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/region.h>

#include "effect/blur.h"
#include "render/pass.h"
#include "render/pixel_format.h"
#include "scene_p.h"

struct ky_scene_buffer *ky_scene_buffer_from_node(struct ky_scene_node *node)
{
    assert(node->type == KY_SCENE_NODE_BUFFER);
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

static bool buffer_is_opaque(struct wlr_buffer *buffer)
{
    void *data;
    uint32_t format;
    size_t stride;
    struct wlr_dmabuf_attributes dmabuf;
    struct wlr_shm_attributes shm;
    if (wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
        format = dmabuf.format;
    } else if (wlr_buffer_get_shm(buffer, &shm)) {
        format = shm.format;
    } else if (wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data,
                                                &format, &stride)) {
        wlr_buffer_end_data_ptr_access(buffer);
    } else {
        return false;
    }

    const struct ky_pixel_format *format_info = ky_pixel_format_from_drm(format);
    if (format_info == NULL) {
        return false;
    }

    return !format_info->has_alpha;
}

static void buffer_get_opaque_region(struct ky_scene_buffer *scene_buffer, int width, int height,
                                     pixman_region32_t *region)
{
    if (!scene_buffer->buffer || scene_buffer->opacity != 1) {
        return;
    }

    if (buffer_is_opaque(scene_buffer->buffer)) {
        pixman_region32_init_rect(region, 0, 0, width, height);
    } else if (pixman_region32_not_empty(&scene_buffer->opaque_region)) {
        pixman_region32_intersect_rect(region, &scene_buffer->opaque_region, 0, 0, width, height);
    }
}

static struct ky_scene_node *buffer_accpet_input(struct ky_scene_node *node, int lx, int ly,
                                                 double px, double py, double *rx, double *ry)
{
    /* skip disabled or input bypassed nodes */
    if (!node->enabled || node->input_bypassed) {
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

    if (pixman_region32_not_empty(&node->input_region) &&
        !pixman_region32_contains_point(&node->input_region, box.x - lx, box.y - ly, NULL)) {
        return NULL;
    }

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

static void buffer_collect_damage(struct ky_scene_node *node, int lx, int ly, bool parent_enabled,
                                  uint32_t damage_type, pixman_region32_t *damage,
                                  pixman_region32_t *invisible, pixman_region32_t *affected)
{
    bool node_enabled = parent_enabled && node->enabled;

    /* node is still disabled, skip it */
    if (!node_enabled && !node->last_enabled) {
        node->damage_type = KY_SCENE_DAMAGE_NONE;
        return;
    }

    struct ky_scene_buffer *scene_buffer = ky_scene_buffer_from_node(node);
    int width, height;
    buffer_get_dest_size(scene_buffer, &width, &height);
    // if node state is changed, it must in the affected region
    if (width > 0 && height > 0 &&
        pixman_region32_contains_rectangle(
            affected, &(pixman_box32_t){ lx, ly, lx + width, ly + height }) == PIXMAN_REGION_OUT) {
        return;
    }

    bool no_damage = node->last_enabled && node_enabled && damage_type == KY_SCENE_DAMAGE_NONE;
    if (!no_damage) {
        /* node last visible region is added to damgae */
        if (node->last_enabled && (!node_enabled || (damage_type & KY_SCENE_DAMAGE_HARMFUL))) {
            pixman_region32_union(damage, damage, &node->visible_region);
        }
    }

    bool visible = node_enabled && scene_buffer->opacity != 0 && scene_buffer->buffer;
    pixman_region32_clear(&node->visible_region);

    if (visible) {
        bool has_clip_region = pixman_region32_not_empty(&node->clip_region);
        if (has_clip_region) {
            pixman_region32_intersect_rect(&node->visible_region, &node->clip_region, 0, 0, width,
                                           height);
            pixman_region32_translate(&node->visible_region, lx, ly);
        } else {
            pixman_region32_init_rect(&node->visible_region, lx, ly, width, height);
        }
        pixman_region32_subtract(&node->visible_region, &node->visible_region, invisible);

        if (!no_damage) {
            pixman_region32_union(damage, damage, &node->visible_region);
        }

        pixman_region32_t region;
        pixman_region32_init(&region);
        buffer_get_opaque_region(scene_buffer, width, height, &region);
        if (has_clip_region) {
            pixman_region32_intersect(&region, &region, &node->clip_region);
        }

        /* substract round corners */
        pixman_region32_t corner;
        pixman_region32_init(&corner);
        ky_scene_corner_region(&corner, width, height, node->radius);
        pixman_region32_subtract(&region, &region, &corner);
        pixman_region32_fini(&corner);

        pixman_region32_translate(&region, lx, ly);
        pixman_region32_union(invisible, invisible, &region);
        pixman_region32_fini(&region);
    }

    node->last_enabled = node_enabled;
    node->damage_type = KY_SCENE_DAMAGE_NONE;
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
    if (!scene_buffer->buffer || scene_buffer->opacity == 0) {
        return;
    }

    bool render_with_visibility = !(target->options & KY_SCENE_RENDER_DISABLE_VISIBILITY);
    if (render_with_visibility && !pixman_region32_not_empty(&node->visible_region)) {
        return;
    }

    pixman_region32_t render_region;
    pixman_region32_init(&render_region);
    if (render_with_visibility) {
        pixman_region32_intersect(&render_region, &node->visible_region, &target->damage);
    } else {
        pixman_region32_copy(&render_region, &target->damage);
    }

    if (!pixman_region32_not_empty(&render_region)) {
        pixman_region32_fini(&render_region);
        return;
    }

    struct wlr_texture *texture =
        scene_buffer_get_texture(scene_buffer, target->output->output->renderer);
    if (texture == NULL) {
        pixman_region32_fini(&render_region);
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
    ky_scene_render_box(&dst_box, target);

    pixman_region32_translate(&render_region, -target->logical.x, -target->logical.y);

    pixman_region32_t opaque;
    pixman_region32_init(&opaque);
    buffer_get_opaque_region(scene_buffer, width, height, &opaque);
    pixman_region32_subtract(&opaque, &render_region, &opaque);

    ky_scene_render_region(&render_region, target);

    enum wl_output_transform transform = wlr_output_transform_invert(scene_buffer->transform);
    transform = wlr_output_transform_compose(transform, target->transform);

    bool render_with_radius = !(target->options & KY_SCENE_RENDER_DISABLE_ROUND_CORNER);
    struct ky_render_texture_options options = {
        .base = {
            .texture = texture,
            .src_box = scene_buffer->src_box,
            .dst_box = dst_box,
            .transform = transform,
            .alpha = &scene_buffer->opacity,
            .clip = &render_region,
            .blend_mode = pixman_region32_not_empty(&opaque) ?
                WLR_RENDER_BLEND_MODE_PREMULTIPLIED : WLR_RENDER_BLEND_MODE_NONE,
        },
        .radius = {
            .rb = render_with_radius ? node->radius[0] * target->scale : 0,
            .rt = render_with_radius ? node->radius[1] * target->scale : 0,
            .lb = render_with_radius ? node->radius[2] * target->scale : 0,
            .lt = render_with_radius ? node->radius[3] * target->scale : 0,
        },
    };

    struct blur_render_options opts = {
        .lx = lx,
        .ly = ly,
        .dst_box = &dst_box,
        .clip = &render_region,
        .radius = &options.radius,
        .region = node->has_blur ? &node->blur_region : NULL,
        .strength = node->blur_strength,
    };
    blur_render_with_target(target, &opts);

    ky_render_pass_add_texture(target->render_pass, &options);

    pixman_region32_fini(&render_region);
    pixman_region32_fini(&opaque);

    struct ky_scene_output_sample_event sample_event = {
        .output = target->output,
        .direct_scanout = false,
    };
    wl_signal_emit_mutable(&scene_buffer->events.output_sample, &sample_event);
}

static void buffer_get_bounding_box(struct ky_scene_node *node, struct wlr_box *box)
{
    struct ky_scene_buffer *scene_buffer = ky_scene_buffer_from_node(node);
    if (!node->enabled || !scene_buffer->buffer || scene_buffer->opacity == 0) {
        *box = (struct wlr_box){ 0 };
        return;
    }

    box->x = 0, box->y = 0;
    buffer_get_dest_size(scene_buffer, &box->width, &box->height);
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

    if (node->last_enabled) {
        struct ky_scene *scene = ky_scene_from_node(node);
        ky_scene_add_damage(scene, &node->visible_region);
    }

    pixman_region32_fini(&scene_buffer->opaque_region);
    scene_buffer->node_destroy(node);
}

void ky_scene_buffer_init(struct ky_scene_buffer *scene_buffer, struct ky_scene_tree *parent)
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
    scene_buffer->node.impl.collect_damage = buffer_collect_damage;
    scene_buffer->node.impl.render = buffer_render;
    scene_buffer->node.impl.get_bounding_box = buffer_get_bounding_box;

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

    ky_scene_buffer_init(scene_buffer, parent);

    if (buffer) {
        scene_buffer->buffer = wlr_buffer_lock(buffer);
        ky_scene_node_push_damage(&scene_buffer->node, KY_SCENE_DAMAGE_HARMFUL, NULL);
        ky_scene_node_update_outputs(&scene_buffer->node, NULL, NULL, NULL);
    }

    return scene_buffer;
}

static void buffer_get_scene_damage(struct ky_scene_buffer *scene_buffer, pixman_region32_t *dst,
                                    const pixman_region32_t *src)
{
    struct wlr_buffer *buffer = scene_buffer->buffer;
    struct wlr_fbox box = scene_buffer->src_box;
    if (wlr_fbox_empty(&box)) {
        box = (struct wlr_fbox){ 0, 0, buffer->width, buffer->height };
    }

    wlr_fbox_transform(&box, &box, scene_buffer->transform, buffer->width, buffer->height);

    float scale_x, scale_y;
    int width, height;
    buffer_get_dest_size(scene_buffer, &width, &height);
    scale_x = width / box.width;
    scale_y = height / box.height;

    wlr_region_transform(dst, src, scene_buffer->transform, buffer->width, buffer->height);
    pixman_region32_intersect_rect(dst, dst, box.x, box.y, box.width, box.height);
    pixman_region32_translate(dst, -box.x, -box.y);
    wlr_region_scale_xy(dst, dst, scale_x, scale_y);
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
        if (damage && pixman_region32_not_empty(damage)) {
            pixman_region32_t region;
            pixman_region32_init(&region);
            buffer_get_scene_damage(scene_buffer, &region, damage);
            ky_scene_node_push_damage(&scene_buffer->node, KY_SCENE_DAMAGE_HARMLESS, &region);
            pixman_region32_fini(&region);
        } else {
            ky_scene_node_push_damage(&scene_buffer->node, KY_SCENE_DAMAGE_HARMLESS, NULL);
        }
        return;
    }

    if (old_width > new_width || old_height > new_height) {
        pixman_region32_t region;
        pixman_region32_init_rect(&region, 0, 0, old_width, old_height);
        ky_scene_node_push_damage(&scene_buffer->node, KY_SCENE_DAMAGE_HARMFUL, &region);
        pixman_region32_fini(&region);
    } else if (old_width < new_width || old_height < new_height) {
        ky_scene_node_push_damage(&scene_buffer->node, KY_SCENE_DAMAGE_HARMFUL, NULL);
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
    ky_scene_node_push_damage(&scene_buffer->node, KY_SCENE_DAMAGE_HARMFUL, NULL);
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

    ky_scene_node_push_damage(&scene_buffer->node, KY_SCENE_DAMAGE_HARMLESS, NULL);
}

void ky_scene_buffer_set_dest_size(struct ky_scene_buffer *scene_buffer, int width, int height)
{
    if (scene_buffer->dst_width == width && scene_buffer->dst_height == height) {
        return;
    }

    bool update_later = false;
    if (scene_buffer->dst_width > width || scene_buffer->dst_height > height) {
        ky_scene_node_push_damage(&scene_buffer->node, KY_SCENE_DAMAGE_HARMFUL, NULL);
    } else if (scene_buffer->dst_width < width || scene_buffer->dst_height < height) {
        update_later = true;
    }

    scene_buffer->dst_width = width;
    scene_buffer->dst_height = height;

    if (update_later) {
        ky_scene_node_push_damage(&scene_buffer->node, KY_SCENE_DAMAGE_HARMFUL, NULL);
    }

    ky_scene_node_update_outputs(&scene_buffer->node, NULL, NULL, NULL);
}

void ky_scene_buffer_set_transform(struct ky_scene_buffer *scene_buffer,
                                   enum wl_output_transform transform)
{
    if (scene_buffer->transform == transform) {
        return;
    }

    // buffer size is used
    if (scene_buffer->dst_width <= 0 || scene_buffer->dst_height <= 0) {
        ky_scene_node_push_damage(&scene_buffer->node, KY_SCENE_DAMAGE_HARMFUL, NULL);
    }

    scene_buffer->transform = transform;

    ky_scene_node_push_damage(&scene_buffer->node, KY_SCENE_DAMAGE_HARMLESS, NULL);
}

void ky_scene_buffer_set_opacity(struct ky_scene_buffer *scene_buffer, float opacity)
{
    if (scene_buffer->opacity == opacity) {
        return;
    }

    scene_buffer->opacity = opacity;
    ky_scene_node_push_damage(&scene_buffer->node, KY_SCENE_DAMAGE_HARMFUL, NULL);
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
