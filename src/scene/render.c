// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <wlr/types/wlr_output.h>
#include <wlr/util/region.h>

#include "effect/blur.h"
#include "effect/effect.h"
#include "render/pass.h"
#include "render/profile.h"
#include "scene/render.h"
#include "util/region.h"

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
    ky_scene_output_render_begin(target);

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

    if (!ky_scene_output_render(target)) {
        struct ky_scene_node *root = &scene->tree.node;
        // render each node with damage region and visible region
        root->impl.render(root, root->x, root->y, target);
    }

    ky_scene_output_render_end(target);
}

void ky_scene_render_target_add_software_cursors(struct ky_scene_render_target *target)
{
    struct wlr_output *output = target->output->output;
    bool need_render = false;

    pixman_region32_t damage;
    pixman_region32_init(&damage);

    struct wlr_output_cursor *cursor;
    wl_list_for_each(cursor, &output->cursors, link) {
        if ((!cursor->enabled || !cursor->visible || output->hardware_cursor == cursor) &&
            !(target->options & KY_SCENE_RENDER_ENABLE_CURSORS)) {
            continue;
        }

        struct wlr_texture *texture = cursor->texture;
        if (texture == NULL) {
            continue;
        }

        if (!need_render) {
            pixman_region32_copy(&damage, &target->damage);
            pixman_region32_translate(&damage, -target->logical.x, -target->logical.y);
            ky_scene_render_region(&damage, target);
            pixman_region32_subtract(&damage, &damage, &target->excluded_buffer_damage);
            need_render = true;
        }

        /* cursor is the buffer coordinate of the output, but does not include rotation */
        /* lx ly is the logical coordinate in the scene */
        float scale = target->output->output->scale;
        int lx = (cursor->x - cursor->hotspot_x) / scale + target->output->x;
        int ly = (cursor->y - cursor->hotspot_y) / scale + target->output->y;

        struct wlr_box box = {
            .x = lx - target->logical.x,
            .y = ly - target->logical.y,

            .width = cursor->width / scale,
            .height = cursor->height / scale,
        };

        ky_scene_render_box(&box, target);

        pixman_region32_t cursor_damage;
        pixman_region32_init_rect(&cursor_damage, box.x, box.y, box.width, box.height);
        pixman_region32_intersect(&cursor_damage, &cursor_damage, &damage);
        if (!pixman_region32_not_empty(&cursor_damage)) {
            pixman_region32_fini(&cursor_damage);
            continue;
        }

        struct wlr_render_texture_options options = {
            .texture = texture,
            .src_box = cursor->src_box,
            .dst_box = box,
            .clip = &cursor_damage,
            .transform = output->transform,
        };
        wlr_render_pass_add_texture(target->render_pass, &options);
        pixman_region32_fini(&cursor_damage);
    }

    pixman_region32_fini(&damage);
}

void ky_scene_render_target_add_texture(struct ky_scene_render_target *target,
                                        const struct ky_scene_render_texture_options *opts)
{
    struct wlr_box dst_box = {
        .x = opts->geometry_box->x - target->logical.x,
        .y = opts->geometry_box->y - target->logical.y,
        .width = opts->geometry_box->width,
        .height = opts->geometry_box->height,
    };
    ky_scene_render_box(&dst_box, target);

    pixman_region32_t render_region;
    pixman_region32_init(&render_region);
    pixman_region32_copy(&render_region, &target->damage);
    pixman_region32_translate(&render_region, -target->logical.x, -target->logical.y);
    ky_scene_render_region(&render_region, target);

    pixman_region32_intersect_rect(&render_region, &render_region, dst_box.x, dst_box.y,
                                   dst_box.width, dst_box.height);
    if (!pixman_region32_not_empty(&render_region)) {
        pixman_region32_fini(&render_region);
        return;
    }

    enum wl_output_transform transform = wlr_output_transform_invert(opts->transform);
    transform = wlr_output_transform_compose(transform, target->transform);

    struct ky_render_texture_options tex_opts = {
        .base = {
            .texture = opts->texture,
            .dst_box = dst_box,
            .transform = transform,
            .alpha = opts->alpha,
            .clip =  &render_region,
        },
        .radius = {
            .rb = opts->radius ? (*opts->radius)[0] * target->scale : 0,
            .rt = opts->radius ? (*opts->radius)[1] * target->scale : 0,
            .lb = opts->radius ? (*opts->radius)[2] * target->scale : 0,
            .lt = opts->radius ? (*opts->radius)[3] * target->scale : 0,
        },
        .rotation_angle = opts->angle,
    };

    if (opts->src) {
        tex_opts.base.src_box.x = opts->src->x;
        tex_opts.base.src_box.y = opts->src->y;
        tex_opts.base.src_box.width = opts->src->width;
        tex_opts.base.src_box.height = opts->src->height;
    }

    KY_PROFILE_RENDER_ZONE(ky_render_pass_get_renderer(target->render_pass), gzone, __func__);
    if (!(target->options & KY_SCENE_RENDER_DISABLE_BLUR) && opts->blur.info) {
        const struct blur_info *info = opts->blur.info;
        struct blur_info blur = {
            .iterations = info->iterations,
            .offset = info->offset,
            .custom_level = info->custom_level,
        };
        pixman_region32_init(&blur.region);

        /* region in texture */
        pixman_region32_copy(&blur.region, &info->region);
        pixman_region32_translate(&blur.region, opts->blur.offset_x, opts->blur.offset_y);
        if (opts->blur.scale) {
            wlr_region_scale(&blur.region, &blur.region, *opts->blur.scale);
        }

        struct wlr_fbox *src_fbox = &tex_opts.base.src_box;
        if (pixman_region32_not_empty(&blur.region) && !wlr_fbox_empty(src_fbox)) {
            pixman_region32_intersect_rect(&blur.region, &blur.region, src_fbox->x, src_fbox->y,
                                           src_fbox->width, src_fbox->height);
            pixman_region32_translate(&blur.region, -src_fbox->x, -src_fbox->y);
        }

        /* region in tex geometry box, logic coord */
        float scale_w = opts->geometry_box->width * 1.0f / opts->texture->width;
        float scale_h = opts->geometry_box->height * 1.0f / opts->texture->height;
        if (!wlr_fbox_empty(src_fbox)) {
            scale_w = opts->geometry_box->width * 1.0f / src_fbox->width;
            scale_h = opts->geometry_box->height * 1.0f / src_fbox->height;
        }
        if (scale_w != 1.0f || scale_h != 1.0f) {
            wlr_region_scale_xy(&blur.region, &blur.region, scale_w, scale_h);
        }
        if (opts->blur.scale && (*opts->blur.scale != floor(*opts->blur.scale))) {
            ky_region_expand(&blur.region, &blur.region, -2);
        }

        struct ky_render_round_corner radius = {
            .rb = opts->blur.radius ? (*opts->blur.radius)[0] * target->scale : 0,
            .rt = opts->blur.radius ? (*opts->blur.radius)[1] * target->scale : 0,
            .lb = opts->blur.radius ? (*opts->blur.radius)[2] * target->scale : 0,
            .lt = opts->blur.radius ? (*opts->blur.radius)[3] * target->scale : 0,
        };

        /* blur region in tex geometry box, logic coord */
        struct blur_render_options blur_opts = {
            .lx = opts->geometry_box->x,
            .ly = opts->geometry_box->y,
            .dst_box = &dst_box,
            .clip = &render_region,
            .radius = opts->blur.radius ? &radius : NULL,
            .blur = pixman_region32_not_empty(&blur.region) ? &blur : NULL,
            .alpha = opts->blur.alpha,
        };
        blur_render_with_target(target, &blur_opts);
        pixman_region32_fini(&blur.region);
    }

    ky_render_pass_add_texture(target->render_pass, &tex_opts);
    pixman_region32_fini(&render_region);
    KY_PROFILE_RENDER_ZONE_END(ky_render_pass_get_renderer(target->render_pass));
}
