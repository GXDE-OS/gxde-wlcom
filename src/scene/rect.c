// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_output.h>

#include "scene_p.h"

struct ky_scene_rect *ky_scene_rect_from_node(struct ky_scene_node *node)
{
    struct ky_scene_rect *rect = wl_container_of(node, rect, node);
    return rect;
};

static struct ky_scene_node *rect_accpet_input(struct ky_scene_node *node, int lx, int ly,
                                               double px, double py, double *rx, double *ry)
{
    /* skip disabled or input bypassed nodes */
    if (!node->enabled || node->bypassed) {
        return NULL;
    }

    struct ky_scene_rect *rect = ky_scene_rect_from_node(node);
    struct wlr_box box = { floor(px), floor(py), 1, 1 };
    struct wlr_box node_box = { lx, ly, rect->width, rect->height };

    if (!wlr_box_intersection(&node_box, &node_box, &box)) {
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

    struct wlr_box dst_box = {
        .x = lx - target->logical.x,
        .y = ly - target->logical.y,
        .width = rect->width,
        .height = rect->height,
    };

    struct wlr_box clip_box;
    if (!ky_scene_render_box(&clip_box, &dst_box, target)) {
        return;
    }

    pixman_region32_t render_region;
    pixman_region32_init_rect(&render_region, clip_box.x, clip_box.y, clip_box.width,
                              clip_box.height);
    ky_scene_render_region(&render_region, target);

    wlr_render_pass_add_rect(target->render_pass, &(struct wlr_render_rect_options){
			.box = dst_box,
			.color = {
				.r = rect->color[0],
				.g = rect->color[1],
				.b = rect->color[2],
				.a = rect->color[3],
			},
            .clip = &render_region,
		});

    pixman_region32_fini(&render_region);
}

static void scene_rect_init(struct ky_scene_rect *rect, struct ky_scene_tree *parent, int width,
                            int height, const float color[static 4])
{
    *rect = (struct ky_scene_rect){ 0 };
    ky_scene_node_init(&rect->node, parent);

    rect->node.type = KY_SCENE_NODE_RECT;

    rect->node.impl.accpet_input = rect_accpet_input;
    rect->node.impl.update_outputs = rect_update_outputs;
    rect->node.impl.render = rect_render;

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

    scene_rect_init(scene_rect, parent, width, height, color);

    return scene_rect;
}

void ky_scene_rect_set_size(struct ky_scene_rect *rect, int width, int height)
{
    if (rect->width == width && rect->height == height) {
        return;
    }

    rect->width = width;
    rect->height = height;
}

void ky_scene_rect_set_color(struct ky_scene_rect *rect, const float color[static 4])
{
    if (memcmp(rect->color, color, sizeof(rect->color)) == 0) {
        return;
    }

    memcpy(rect->color, color, sizeof(rect->color));
}
