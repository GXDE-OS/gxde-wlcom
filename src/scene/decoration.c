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
    /* shadow part need be shown */
    uint32_t shadow_mask;

    pixman_region32_t title_region;
    pixman_region32_t border_region;
    pixman_region32_t shadow_region;
};

static void scene_decoration_update_region(struct ky_scene_decoration *scene_decoration)
{
    pixman_region32_t clip;
    pixman_region32_init(&clip);

    pixman_region32_clear(&scene_decoration->title_region);
    pixman_region32_clear(&scene_decoration->border_region);
    pixman_region32_clear(&scene_decoration->shadow_region);

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
        pixman_region32_init_rect(&scene_decoration->title_region, x, x, width, title);

        pixman_region32_t reg1;
        pixman_region32_init_rect(&reg1, shadow, shadow, scene_decoration->rect.width - 2 * shadow,
                                  scene_decoration->rect.height - 2 * shadow);
        pixman_region32_subtract(&scene_decoration->shadow_region, &clip, &reg1);

        pixman_region32_t reg2;
        pixman_region32_init_rect(&reg2, x, x, width, title + height);
        pixman_region32_subtract(&scene_decoration->border_region, &reg1, &reg2);
        pixman_region32_fini(&reg1);
        pixman_region32_fini(&reg2);

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

    scene_decoration_update_region(scene_decoration);
    scene_decoration_update_input_region(scene_decoration);
}

struct ky_scene_decoration *ky_scene_decoration_from_node(struct ky_scene_node *node)
{
    struct ky_scene_rect *rect = ky_scene_rect_from_node(node);
    struct ky_scene_decoration *scene_decoration = wl_container_of(rect, scene_decoration, rect);
    return scene_decoration;
}

struct ky_scene_node *ky_scene_node_from_decoration(struct ky_scene_decoration *scene_decoration)
{
    return &scene_decoration->rect.node;
}

static void scene_decoration_collect_damage(struct ky_scene_node *node, int lx, int ly,
                                            bool parent_enabled, uint32_t damage_type,
                                            pixman_region32_t *damage, pixman_region32_t *invisible,
                                            pixman_region32_t *affected)
{
    bool node_enabled = parent_enabled && node->enabled;
    /* node is still disabled, skip it */
    if (!node_enabled && !node->last_enabled) {
        node->damage_type = KY_SCENE_DAMAGE_NONE;
        return;
    }

    struct ky_scene_decoration *deco = ky_scene_decoration_from_node(node);
    // if node state is changed, it must in the affected region
    if (deco->rect.width > 0 && deco->rect.height > 0 &&
        pixman_region32_contains_rectangle(
            affected, &(pixman_box32_t){ lx, ly, lx + deco->rect.width, ly + deco->rect.height }) ==
            PIXMAN_REGION_OUT) {
        return;
    }

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

    if (node_enabled) {
        // always have clip region
        pixman_region32_intersect_rect(&node->visible_region, &node->clip_region, 0, 0,
                                       deco->rect.width, deco->rect.height);
        pixman_region32_translate(&node->visible_region, lx, ly);
        pixman_region32_subtract(&node->visible_region, &node->visible_region, invisible);

        if (!no_damage) {
            pixman_region32_union(damage, damage, &node->visible_region);
        }

        bool border_is_opaque = deco->border_thickness > 0 && deco->border_color[3] == 1;
        bool title_is_opaque = deco->title_height > 0 && deco->title_color[3] == 1;
        bool has_opaque_region = border_is_opaque || title_is_opaque;
        if (has_opaque_region) {
            pixman_region32_t region;
            pixman_region32_init(&region);
            if (border_is_opaque) {
                pixman_region32_union(&region, &region, &deco->border_region);
            }
            if (title_is_opaque) {
                pixman_region32_union(&region, &region, &deco->title_region);
            }
            pixman_region32_translate(&region, lx, ly);
            pixman_region32_union(invisible, invisible, &region);
            pixman_region32_fini(&region);
        }
    }

    node->last_enabled = node_enabled;
    node->damage_type = KY_SCENE_DAMAGE_NONE;
}

static void scene_decoration_render(struct ky_scene_node *node, int lx, int ly,
                                    struct ky_scene_render_target *target)
{
    if (!node->enabled) {
        return;
    }

    if (!pixman_region32_not_empty(&node->visible_region)) {
        return;
    }

    pixman_region32_t render_region;
    pixman_region32_init(&render_region);
    pixman_region32_intersect(&render_region, &node->visible_region, &target->damage);

    if (!pixman_region32_not_empty(&render_region)) {
        pixman_region32_fini(&render_region);
        return;
    }

    struct ky_scene_decoration *deco = ky_scene_decoration_from_node(node);

    struct wlr_box dst_box = {
        .x = lx - target->logical.x,
        .y = ly - target->logical.y,
        .width = deco->rect.width,
        .height = deco->rect.height,
    };
    ky_scene_render_box(&dst_box, target);

    pixman_region32_translate(&render_region, -target->logical.x, -target->logical.y);

    /* draw border with border color */
    if (deco->border_thickness > 0) {
        pixman_region32_t border;
        pixman_region32_init(&border);
        pixman_region32_copy(&border, &deco->border_region);
        pixman_region32_translate(&border, lx - target->logical.x, ly - target->logical.y);
        pixman_region32_intersect(&border, &border, &render_region);
        ky_scene_render_region(&border, target);

        wlr_render_pass_add_rect(target->render_pass, &(struct wlr_render_rect_options){
			.box = dst_box,
			.color = {
				.r = deco->border_color[0],
				.g = deco->border_color[1],
				.b = deco->border_color[2],
				.a = deco->border_color[3],
			},
            .clip = &border,
            .blend_mode = deco->border_color[3] != 1 ? 
                WLR_RENDER_BLEND_MODE_PREMULTIPLIED : WLR_RENDER_BLEND_MODE_NONE,
		});
        pixman_region32_fini(&border);
    }

    /* draw title with title color */
    if (deco->title_height > 0) {
        pixman_region32_t title;
        pixman_region32_init(&title);
        pixman_region32_copy(&title, &deco->title_region);
        pixman_region32_translate(&title, lx - target->logical.x, ly - target->logical.y);
        pixman_region32_intersect(&title, &title, &render_region);
        ky_scene_render_region(&title, target);

        wlr_render_pass_add_rect(target->render_pass, &(struct wlr_render_rect_options){
			.box = dst_box,
			.color = {
				.r = deco->title_color[0],
				.g = deco->title_color[1],
				.b = deco->title_color[2],
				.a = deco->title_color[3],
			},
            .clip = &title,
            .blend_mode = deco->title_color[3] != 1 ? 
                WLR_RENDER_BLEND_MODE_PREMULTIPLIED : WLR_RENDER_BLEND_MODE_NONE,
		});
        pixman_region32_fini(&title);
    }

    pixman_region32_fini(&render_region);
}

static void scene_decoration_destroy(struct ky_scene_node *node)
{
    if (!node) {
        return;
    }

    struct ky_scene_decoration *scene_decoration = ky_scene_decoration_from_node(node);
    pixman_region32_fini(&scene_decoration->title_region);
    pixman_region32_fini(&scene_decoration->border_region);
    pixman_region32_fini(&scene_decoration->shadow_region);

    scene_decoration->node_destroy(node);
}

struct ky_scene_decoration *ky_scene_decoration_create(struct ky_scene_tree *parent)
{
    struct ky_scene_decoration *scene_decoration = calloc(1, sizeof(struct ky_scene_decoration));
    if (!scene_decoration) {
        return NULL;
    }

    ky_scene_rect_init(&scene_decoration->rect, parent, 0, 0, (float[4]){ 0, 0, 0, 0.5 });

    scene_decoration->node_destroy = scene_decoration->rect.node.impl.destroy;
    scene_decoration->rect.node.impl.destroy = scene_decoration_destroy;
    scene_decoration->rect.node.impl.collect_damage = scene_decoration_collect_damage;
    scene_decoration->rect.node.impl.render = scene_decoration_render;
    /* no need to update_region and push_damage, it is invisible */

    pixman_region32_init(&scene_decoration->title_region);
    pixman_region32_init(&scene_decoration->border_region);
    pixman_region32_init(&scene_decoration->shadow_region);

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

    memcpy(scene_decoration->title_color, title_color, sizeof(scene_decoration->title_color));
    memcpy(scene_decoration->border_color, border_color, sizeof(scene_decoration->border_color));
    memcpy(scene_decoration->shadow_color, shadow_color, sizeof(scene_decoration->shadow_color));

    ky_scene_node_push_damage(&scene_decoration->rect.node, KY_SCENE_DAMAGE_HARMFUL,
                              &scene_decoration->rect.node.clip_region);
}

void ky_scene_decoration_set_shadow_mask(struct ky_scene_decoration *scene_decoration,
                                         uint32_t shadow_mask)
{
    if (scene_decoration->shadow_mask == shadow_mask) {
        return;
    }

    scene_decoration->shadow_mask = shadow_mask;
    ky_scene_node_push_damage(&scene_decoration->rect.node, KY_SCENE_DAMAGE_HARMFUL,
                              &scene_decoration->shadow_region);
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
