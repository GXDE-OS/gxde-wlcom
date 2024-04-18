// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <wlr/types/wlr_output.h>
#include <wlr/util/region.h>

#include "scene/render.h"

static int scale_length(int length, int offset, float scale)
{
    return round((offset + length) * scale) - round(offset * scale);
}

void ky_scene_render_box(struct wlr_box *box, struct ky_scene_render_target *target)
{
    box->width = scale_length(box->width, box->x, target->scale);
    box->height = scale_length(box->height, box->y, target->scale);
    box->x = round(box->x * target->scale);
    box->y = round(box->y * target->scale);

    enum wl_output_transform transform = wlr_output_transform_invert(target->transform);
    wlr_box_transform(box, box, transform, target->trans_width, target->trans_height);
}

void ky_scene_render_region(pixman_region32_t *region, struct ky_scene_render_target *target)
{
    wlr_region_scale(region, region, target->scale);

    enum wl_output_transform transform = wlr_output_transform_invert(target->transform);
    wlr_region_transform(region, region, transform, target->trans_width, target->trans_height);
}

void ky_scene_render_damage_in_target(struct ky_scene *scene, struct ky_scene_render_target *target)
{
    if (!pixman_region32_not_empty(&target->damage)) {
        return;
    }

    // to scene layout coord
    pixman_region32_translate(&target->damage, target->logical.x, target->logical.y);

    // clear current output buffer damage region
    pixman_region32_t background;
    pixman_region32_init(&background);
    pixman_region32_subtract(&background, &target->damage, &scene->collected_invisible);
    pixman_region32_translate(&background, -target->logical.x, -target->logical.y);
    ky_scene_render_region(&background, target);
    wlr_render_pass_add_rect(target->render_pass, &(struct wlr_render_rect_options){
                                                      .box = { .width = target->buffer->width,
                                                               .height = target->buffer->height },
                                                      .color = { .r = 0, .g = 0, .b = 0, .a = 1 },
                                                      .clip = &background,
                                                  });
    pixman_region32_fini(&background);

    struct ky_scene_node *root = &scene->tree.node;
    // render each node with damage region and visible region
    root->impl.render(root, root->x, root->y, target);

    // for software cursor
    pixman_region32_translate(&target->damage, -target->logical.x, -target->logical.y);
    wlr_region_scale(&target->damage, &target->damage, target->scale);
    wlr_output_add_software_cursors_to_render_pass(target->output->output, target->render_pass,
                                                   &target->damage);
}
