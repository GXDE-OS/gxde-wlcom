// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_output.h>

#include "effect/blur.h"
#include "render/opengl.h"
#include "render/pass.h"
#include "scene_p.h"

struct ky_scene_rect *ky_scene_rect_from_node(struct ky_scene_node *node)
{
    assert(node->type == KY_SCENE_NODE_RECT);
    struct ky_scene_rect *rect = wl_container_of(node, rect, node);
    return rect;
};

static struct ky_scene_node *rect_accpet_input(struct ky_scene_node *node, int lx, int ly,
                                               double px, double py, double *rx, double *ry)
{
    /* skip disabled or input bypassed nodes */
    if (!node->enabled || node->input_bypassed) {
        return NULL;
    }

    struct ky_scene_rect *rect = ky_scene_rect_from_node(node);
    struct wlr_box box = { floor(px), floor(py), 1, 1 };
    struct wlr_box node_box = { lx, ly, rect->width, rect->height };

    if (!wlr_box_intersection(&node_box, &node_box, &box)) {
        return NULL;
    }

    if (pixman_region32_not_empty(&node->input_region) &&
        !pixman_region32_contains_point(&node->input_region, box.x - lx, box.y - ly, NULL)) {
        return NULL;
    }

    *rx = px - lx;
    *ry = py - ly;
    return node;
}

static void rect_update_outputs(struct ky_scene_node *node, int lx, int ly, struct wl_list *outputs,
                                struct ky_scene_output *ignore, struct ky_scene_output *force)
{
    // Do nothing
}

static void rect_collect_damage(struct ky_scene_node *node, int lx, int ly, bool parent_enabled,
                                uint32_t damage_type, pixman_region32_t *damage,
                                pixman_region32_t *invisible, pixman_region32_t *affected)
{
    bool node_enabled = parent_enabled && node->enabled;
    /* node is still disabled, skip it */
    if (!node_enabled && !node->last_enabled) {
        node->damage_type = KY_SCENE_DAMAGE_NONE;
        return;
    }

    struct ky_scene_rect *rect = ky_scene_rect_from_node(node);
    // if node state is changed, it must in the affected region
    if (rect->width > 0 && rect->height > 0 &&
        pixman_region32_contains_rectangle(
            affected, &(pixman_box32_t){ lx, ly, lx + rect->width, ly + rect->height }) ==
            PIXMAN_REGION_OUT) {
        return;
    }

    /**
     * we may need to do 3 things:
     * 1. add node damage region to the scene collected_damage
     * 2. update node visible region
     * 3. add node opaque region to the scene collected_invisible
     */

    // no damage if node state is not changed
    bool no_damage = node->last_enabled && node_enabled && damage_type == KY_SCENE_DAMAGE_NONE;
    if (!no_damage) {
        /* node last visible region is added to damgae */
        if (node->last_enabled && (!node_enabled || (damage_type & KY_SCENE_DAMAGE_HARMFUL))) {
            pixman_region32_union(damage, damage, &node->visible_region);
        }
    }

    // update node visible region always
    pixman_region32_clear(&node->visible_region);

    if (node_enabled && rect->color[3] != 0) {
        // current visible region
        bool has_clip_region = pixman_region32_not_empty(&node->clip_region);
        if (has_clip_region) {
            pixman_region32_intersect_rect(&node->visible_region, &node->clip_region, 0, 0,
                                           rect->width, rect->height);
            pixman_region32_translate(&node->visible_region, lx, ly);
        } else {
            pixman_region32_init_rect(&node->visible_region, lx, ly, rect->width, rect->height);
        }
        pixman_region32_subtract(&node->visible_region, &node->visible_region, invisible);

        if (!no_damage) {
            pixman_region32_union(damage, damage, &node->visible_region);
        }

        if (rect->color[3] == 1) {
            pixman_region32_t region;
            pixman_region32_init_rect(&region, 0, 0, rect->width, rect->height);
            if (has_clip_region) {
                pixman_region32_intersect(&region, &region, &node->clip_region);
            }

            /* substract round corners */
            pixman_region32_t corner;
            pixman_region32_init(&corner);
            ky_scene_corner_region(&corner, rect->width, rect->height, node->radius);
            pixman_region32_subtract(&region, &region, &corner);
            pixman_region32_fini(&corner);

            pixman_region32_translate(&region, lx, ly);
            pixman_region32_union(invisible, invisible, &region);
            pixman_region32_fini(&region);
        }
    }

    node->last_enabled = node_enabled;
    node->damage_type = KY_SCENE_DAMAGE_NONE;
}

static void rect_render(struct ky_scene_node *node, int lx, int ly,
                        struct ky_scene_render_target *target)
{
    if (!node->enabled) {
        return;
    }

    struct ky_scene_rect *rect = ky_scene_rect_from_node(node);
    if (rect->color[3] == 0) {
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

    struct wlr_box dst_box = {
        .x = lx - target->logical.x,
        .y = ly - target->logical.y,
        .width = rect->width,
        .height = rect->height,
    };
    ky_scene_render_box(&dst_box, target);

    pixman_region32_translate(&render_region, -target->logical.x, -target->logical.y);
    ky_scene_render_region(&render_region, target);

    bool render_with_radius = !(target->options & KY_SCENE_RENDER_DISABLE_ROUND_CORNER);
    struct ky_render_rect_options options = {
        .base = {
            .box = dst_box,
            .color = {
                .r = rect->color[0],
                .g = rect->color[1],
                .b = rect->color[2],
                .a = rect->color[3],
            },
            .clip = &render_region,
            .blend_mode = rect->color[3] != 1 ? 
                WLR_RENDER_BLEND_MODE_PREMULTIPLIED : WLR_RENDER_BLEND_MODE_NONE,
        },
        .radius = {
            .rb = render_with_radius ? node->radius[0] * target->scale : 0,
            .rt = render_with_radius ? node->radius[1] * target->scale : 0,
            .lb = render_with_radius ? node->radius[2] * target->scale : 0,
            .lt = render_with_radius ? node->radius[3] * target->scale : 0,
        },
    };

    if (!(target->options & KY_SCENE_RENDER_DISABLE_BLUR)) {
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
    }

    ky_render_pass_add_rect(target->render_pass, &options);

    pixman_region32_fini(&render_region);
}

static void rect_get_bounding_box(struct ky_scene_node *node, struct wlr_box *box)
{
    struct ky_scene_rect *rect = ky_scene_rect_from_node(node);
    *box = (struct wlr_box){ 0, 0, rect->width, rect->height };
}

static void rect_destroy(struct ky_scene_node *node)
{
    if (!node) {
        return;
    }

    struct ky_scene_rect *rect = ky_scene_rect_from_node(node);
    if (node->last_enabled) {
        struct ky_scene *scene = ky_scene_from_node(node);
        ky_scene_add_damage(scene, &node->visible_region);
    }

    rect->node_destroy(node);
}

void ky_scene_rect_init(struct ky_scene_rect *rect, struct ky_scene_tree *parent, int width,
                        int height, const float color[static 4])
{
    *rect = (struct ky_scene_rect){ 0 };
    ky_scene_node_init(&rect->node, parent);

    rect->node.type = KY_SCENE_NODE_RECT;

    rect->node_destroy = rect->node.impl.destroy;
    rect->node.impl.destroy = rect_destroy;

    rect->node.impl.accpet_input = rect_accpet_input;
    rect->node.impl.update_outputs = rect_update_outputs;
    rect->node.impl.collect_damage = rect_collect_damage;
    rect->node.impl.render = rect_render;
    rect->node.impl.get_bounding_box = rect_get_bounding_box;

    rect->width = width;
    rect->height = height;
    memcpy(rect->color, color, sizeof(rect->color));
}

struct ky_scene_rect *ky_scene_rect_create(struct ky_scene_tree *parent, int width, int height,
                                           const float color[static 4])
{
    struct ky_scene_rect *scene_rect = calloc(1, sizeof(struct ky_scene_rect));
    if (!scene_rect) {
        return NULL;
    }

    ky_scene_rect_init(scene_rect, parent, width, height, color);
    ky_scene_node_push_damage(&scene_rect->node, KY_SCENE_DAMAGE_HARMFUL, NULL);

    return scene_rect;
}

void ky_scene_rect_set_size(struct ky_scene_rect *rect, int width, int height)
{
    if (rect->width == width && rect->height == height) {
        return;
    }

    bool update_later = false;
    if ((rect->width > width || rect->height > height)) {
        ky_scene_node_push_damage(&rect->node, KY_SCENE_DAMAGE_BOTH, NULL);
    }
    if (rect->width < width || rect->height < height) {
        update_later = true;
    }

    rect->width = width;
    rect->height = height;

    if (update_later) {
        ky_scene_node_push_damage(&rect->node, KY_SCENE_DAMAGE_BOTH, NULL);
    }
}

void ky_scene_rect_set_color(struct ky_scene_rect *rect, const float color[static 4])
{
    if (memcmp(rect->color, color, sizeof(rect->color)) == 0) {
        return;
    }

    memcpy(rect->color, color, sizeof(rect->color));

    bool harmful = (rect->color[3] != 1 && color[3] == 1) || (rect->color[3] == 1 && color[3] != 1);
    ky_scene_node_push_damage(&rect->node,
                              harmful ? KY_SCENE_DAMAGE_BOTH : KY_SCENE_DAMAGE_HARMLESS, NULL);
}
