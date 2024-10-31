// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>
#include <string.h>

#include <cairo.h>

#include "scene/box.h"

#define ANGLE(ang) (ang * 3.1415926 / 180.0)

struct ky_scene_box {
    struct ky_scene_rect *rect;
    struct wl_listener destroy;

    int radius[4];
    int border_width;
};

static bool box_create_rounded_region(struct ky_scene_box *scene_box, pixman_region32_t *region)
{
    int width = scene_box->rect->width, height = scene_box->rect->height;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_A1, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        return false;
    }

    cairo_t *cairo = cairo_create(surface);
    double half = scene_box->border_width / 2.0;
    cairo_set_line_width(cairo, scene_box->border_width);

    int lt = scene_box->radius[KY_SCENE_ROUND_CORNER_LT];
    if (lt > 0) {
        cairo_move_to(cairo, half, lt);
        cairo_arc(cairo, lt, lt, lt - half, ANGLE(-180), ANGLE(-90));
    } else {
        cairo_move_to(cairo, half, half);
    }

    int rt = scene_box->radius[KY_SCENE_ROUND_CORNER_RT];
    if (rt > 0) {
        cairo_line_to(cairo, width - rt, half);
        cairo_arc(cairo, width - rt, rt, rt - half, ANGLE(-90), ANGLE(0));
    } else {
        cairo_line_to(cairo, width - half, half);
    }

    int rb = scene_box->radius[KY_SCENE_ROUND_CORNER_RB];
    if (rb > 0) {
        cairo_line_to(cairo, width - half, height - rb);
        cairo_arc(cairo, width - rb, height - rb, rb - half, ANGLE(0), ANGLE(90));
    } else {
        cairo_line_to(cairo, width - half, height - half);
    }

    int lb = scene_box->radius[KY_SCENE_ROUND_CORNER_LB];
    if (lb > 0) {
        cairo_line_to(cairo, lb, height - half);
        cairo_arc(cairo, lb, height - lb, lb - half, ANGLE(90), ANGLE(180));
    } else {
        cairo_line_to(cairo, half, height - half);
    }

    cairo_close_path(cairo);
    cairo_set_source_rgba(cairo, 0, 0, 0, 1);
    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_stroke(cairo);
    cairo_surface_flush(surface);

    void *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    pixman_image_t *image =
        pixman_image_create_bits_no_clear(PIXMAN_a1, width, height, data, stride);
    if (!image) {
        cairo_destroy(cairo);
        cairo_surface_destroy(surface);
        return false;
    }

    pixman_region32_init_from_image(region, image);

    pixman_image_unref(image);
    cairo_destroy(cairo);
    cairo_surface_destroy(surface);

    return true;
}

static bool box_has_radius(struct ky_scene_box *scene_box)
{
    return scene_box->radius[0] > 0 || scene_box->radius[1] > 0 || scene_box->radius[2] > 0 ||
           scene_box->radius[3] > 0;
}

/**
 * update input region and clip region
 */
static void box_update_region(struct ky_scene_box *scene_box)
{
    pixman_region32_t region;
    pixman_region32_init(&region);

    int width = scene_box->rect->width;
    int height = scene_box->rect->height;

    if (width > 0 && height > 0 &&
        (!box_has_radius(scene_box) || !box_create_rounded_region(scene_box, &region))) {
        // fallback to no rounded corners
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

void ky_scene_box_set_radius(struct ky_scene_box *scene_box, const int radius[static 4])
{
    if (memcmp(scene_box->radius, radius, sizeof(scene_box->radius)) == 0) {
        return;
    }

    memcpy(scene_box->radius, radius, sizeof(scene_box->radius));
    box_update_region(scene_box);
}
