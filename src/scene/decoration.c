// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include "scene/decoration.h"
#include "scene_p.h"

struct ky_scene_decoration {
    /* based on scene_rect */
    struct ky_scene_rect rect;
    ky_scene_node_destroy_func_t node_destroy;

    /* window size */
    int window_width;
    int window_height;

    /* margin */
    int border_thickness;
    int title_height;
    int shadow_width;

    /* margin color */
    float border_color[4];
    float title_color[4];
    float shadow_color[4];

    /* region used to resize */
    int resize_width;

    // window round rect
    // 0=right-bottom, 1=right-top, 2=left-bottom, 3=left-top
    int round_corner_radius[4];
};

static void scene_decoration_update_clip_region(struct ky_scene_decoration *scene_decoration)
{
    pixman_region32_t clip;
    pixman_region32_init(&clip);

    int width = scene_decoration->window_width;
    int height = scene_decoration->window_height;
    int shadow = scene_decoration->shadow_width;
    int title = scene_decoration->title_height;
    int border = scene_decoration->border_thickness;

    if (width > 0 && height > 0 && (shadow > 0 || border > 0 || title > 0)) {
        pixman_region32_init_rect(&clip, 0, 0, scene_decoration->rect.width,
                                  scene_decoration->rect.height);

        int x = shadow + border;
        int y = shadow + border + title;
        pixman_region32_t region;
        pixman_region32_init_rect(&region, x, y, width, height);
        pixman_region32_subtract(&clip, &clip, &region);
        pixman_region32_fini(&region);
    }

    ky_scene_node_set_clip_region(&scene_decoration->rect.node, &clip);

    pixman_region32_fini(&clip);
}

static void scene_decoration_update_input_region(struct ky_scene_decoration *scene_decoration)
{
    pixman_region32_t input;
    pixman_region32_init(&input);

    int width = scene_decoration->window_width;
    int height = scene_decoration->window_height;
    int shadow = scene_decoration->shadow_width;
    int border = scene_decoration->border_thickness;
    int resize = scene_decoration->resize_width;

    if (width > 0 && height > 0 && (border > 0 || resize > 0)) {
        int off = shadow - resize; // MUST >= 0
        int w = scene_decoration->rect.width - 2 * off;
        int h = scene_decoration->rect.height - 2 * off;
        pixman_region32_init_rect(&input, off, off, w, h);
    }

    ky_scene_node_set_input_region(&scene_decoration->rect.node, &input);

    pixman_region32_fini(&input);
}

static void scene_decoration_update_size(struct ky_scene_decoration *scene_decoration)
{
    int margin = 2 * (scene_decoration->border_thickness + scene_decoration->shadow_width);
    int width = scene_decoration->window_width + margin;
    int height = scene_decoration->window_height + scene_decoration->title_height + margin;

    if (width != scene_decoration->rect.width || height != scene_decoration->rect.height) {
        ky_scene_rect_set_size(&scene_decoration->rect, width, height);
    }

    scene_decoration_update_clip_region(scene_decoration);
    scene_decoration_update_input_region(scene_decoration);
}

struct ky_scene_decoration *ky_scene_decoration_from_node(struct ky_scene_node *node)
{
    struct ky_scene_rect *rect = ky_scene_rect_from_node(node);
    struct ky_scene_decoration *scene_decoration = wl_container_of(rect, scene_decoration, rect);
    return scene_decoration;
}

static void scene_decoration_destroy(struct ky_scene_node *node)
{
    if (!node) {
        return;
    }

    struct ky_scene_decoration *scene_decoration = ky_scene_decoration_from_node(node);
    scene_decoration->node_destroy(node);
}

struct ky_scene_node *ky_scene_node_from_decoration(struct ky_scene_decoration *scene_decoration)
{
    return &scene_decoration->rect.node;
}

struct ky_scene_decoration *ky_scene_decoration_create(struct ky_scene_tree *parent)
{
    struct ky_scene_decoration *scene_decoration = calloc(1, sizeof(struct ky_scene_decoration));
    if (!scene_decoration) {
        return NULL;
    }

    ky_scene_rect_init(&scene_decoration->rect, parent, 0, 0, (float[4]){ 0.f, 0.f, 0.f, 0.f });

    scene_decoration->node_destroy = scene_decoration->rect.node.impl.destroy;
    scene_decoration->rect.node.impl.destroy = scene_decoration_destroy;
    /* no need to update_region and push_damage, it is invisible */

    return scene_decoration;
}

void ky_scene_decoration_set_window_size(struct ky_scene_decoration *scene_decoration, int width,
                                         int height)
{
    if (scene_decoration->window_width == width && scene_decoration->window_height == height) {
        return;
    }

    scene_decoration->window_width = width;
    scene_decoration->window_height = height;
    scene_decoration_update_size(scene_decoration);
}

void ky_scene_decoration_set_round_corner_radius(struct ky_scene_decoration *scene_decoration,
                                                 const int round_corner_radius[static 4])
{
    if (memcmp(scene_decoration->round_corner_radius, round_corner_radius,
               sizeof(scene_decoration->round_corner_radius)) == 0) {
        return;
    }

    memcpy(scene_decoration->round_corner_radius, round_corner_radius,
           sizeof(scene_decoration->round_corner_radius));
    ky_scene_node_push_damage(&scene_decoration->rect.node, KY_SCENE_DAMAGE_HARMFUL, NULL);
}

void ky_scene_decoration_set_margin(struct ky_scene_decoration *scene_decoration, int title_height,
                                    int border_thickness, int shadow_width)
{
    if (scene_decoration->title_height == title_height &&
        scene_decoration->border_thickness == border_thickness &&
        scene_decoration->shadow_width == shadow_width) {
        return;
    }

    scene_decoration->title_height = title_height;
    scene_decoration->border_thickness = border_thickness;
    scene_decoration->shadow_width = shadow_width;
    scene_decoration_update_size(scene_decoration);
}

void ky_scene_decoration_set_margin_color(struct ky_scene_decoration *scene_decoration,
                                          const float title_color[static 4],
                                          const float border_color[static 4],
                                          const float shadow_color[static 4])
{

    if (memcmp(scene_decoration->title_color, title_color, sizeof(scene_decoration->title_color)) ==
            0 &&
        memcmp(scene_decoration->border_color, border_color,
               sizeof(scene_decoration->border_color)) == 0 &&
        memcmp(scene_decoration->shadow_color, shadow_color,
               sizeof(scene_decoration->shadow_color)) == 0) {
        return;
    }

    memcpy(scene_decoration->shadow_color, title_color, sizeof(scene_decoration->shadow_color));
    memcpy(scene_decoration->title_color, border_color, sizeof(scene_decoration->title_color));
    memcpy(scene_decoration->border_color, shadow_color, sizeof(scene_decoration->border_color));

    // ky_scene_node_push_damage(&scene_decoration->rect.node, KY_SCENE_DAMAGE_HARMFUL, NULL);
    ky_scene_rect_set_color(&scene_decoration->rect, scene_decoration->title_color);
}

void ky_scene_decoration_set_resize_width(struct ky_scene_decoration *scene_decoration,
                                          int resize_with)
{
    if (scene_decoration->resize_width == resize_with) {
        return;
    }

    scene_decoration->resize_width = resize_with;
    scene_decoration_update_input_region(scene_decoration);
}
