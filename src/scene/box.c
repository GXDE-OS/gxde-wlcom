// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include "scene/box.h"

struct ky_scene_box {
    struct ky_scene_rect *rect;
    struct wl_listener destroy;

    int border_width;
};

/**
 * update input region and clip region
 */
static void box_update_region(struct ky_scene_box *scene_box)
{
    pixman_region32_t region;
    pixman_region32_init(&region);

    int width = scene_box->rect->width;
    int height = scene_box->rect->height;

    if (width > 0 && height > 0) {
        pixman_region32_init_rect(&region, 0, 0, width, height);

        int off = scene_box->border_width;
        pixman_region32_t reg;
        pixman_region32_init_rect(&reg, off, off, width - 2 * off, height - 2 * off);
        pixman_region32_subtract(&region, &region, &reg);
        pixman_region32_fini(&reg);
    }

    ky_scene_node_set_input_region(&scene_box->rect->node, &region);
    ky_scene_node_set_clip_region(&scene_box->rect->node, &region);

    pixman_region32_fini(&region);
}

static void box_handle_destroy(struct wl_listener *listener, void *data)
{
    struct ky_scene_box *scene_box = wl_container_of(listener, scene_box, destroy);
    wl_list_remove(&scene_box->destroy.link);
    free(scene_box);
}

struct ky_scene_box *ky_scene_box_create(struct ky_scene_tree *parent, int width, int height,
                                         const float color[static 4], int border_width)
{
    struct ky_scene_box *scene_box = calloc(1, sizeof(struct ky_scene_box));
    if (!scene_box) {
        return NULL;
    }

    scene_box->rect = ky_scene_rect_create(parent, width, height, color);
    if (!scene_box->rect) {
        free(scene_box);
        return NULL;
    }

    scene_box->destroy.notify = box_handle_destroy;
    wl_signal_add(&scene_box->rect->node.events.destroy, &scene_box->destroy);

    scene_box->border_width = border_width;
    box_update_region(scene_box);

    return scene_box;
}

struct ky_scene_node *ky_scene_node_from_box(struct ky_scene_box *scene_box)
{
    return &scene_box->rect->node;
}

void ky_scene_box_set_color(struct ky_scene_box *scene_box, const float color[static 4])
{
    ky_scene_rect_set_color(scene_box->rect, color);
}

void ky_scene_box_set_size(struct ky_scene_box *scene_box, int width, int height)
{
    if (scene_box->rect->width == width && scene_box->rect->height == height) {
        return;
    }

    ky_scene_rect_set_size(scene_box->rect, width, height);
    box_update_region(scene_box);
}

void ky_scene_box_set_border_width(struct ky_scene_box *scene_box, int width)
{
    if (scene_box->border_width == width) {
        return;
    }

    scene_box->border_width = width;
    box_update_region(scene_box);
}
