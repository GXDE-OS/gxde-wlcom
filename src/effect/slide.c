// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include <wlr/render/wlr_texture.h>

#include <kywc/log.h>

#include "effect/animator.h"
#include "effect/slide.h"
#include "effect/transform.h"
#include "effect_p.h"
#include "output.h"
#include "scene/surface.h"
#include "util/time.h"

struct slide_entity {
    struct transform *transform;
    struct ky_scene_node *node;
    int location;
    int offset;

    bool slide_in;

    /* No need to monitor view destroy signal */
    struct wl_listener destroy;
};

struct slide_effect {
    struct transform_effect *effect;
};

static struct slide_effect *slide_effect = NULL;

static void slide_get_node_origin_geometry(struct ky_scene_node *node, struct kywc_box *geometry)
{
    struct wlr_box box;
    node->impl.get_bounding_box(node, &box);
    ky_scene_node_coords(node, &geometry->x, &geometry->y);
    geometry->x += box.x;
    geometry->y += box.y;
    geometry->width = box.width;
    geometry->height = box.height;
}

static void slide_calc_start_and_end_geometry(struct slide_entity *slide_entity,
                                              const struct kywc_box *node_geometry, bool slide_in,
                                              struct kywc_box *start_geometry,
                                              struct kywc_box *end_geometry)
{
    double center_x = node_geometry->x + node_geometry->width / 2.0;
    double center_y = node_geometry->y + node_geometry->height / 2.0;
    struct kywc_output *kywc_output = kywc_output_at_point(center_x, center_y);
    struct output *output = output_from_kywc_output(kywc_output);

    struct kywc_box slide_geometry = *node_geometry;
    switch (slide_entity->location) {
    case SLIDE_LOCATION_LEFT:
        slide_geometry.x = output->geometry.x + slide_entity->offset;
        slide_geometry.width = 0;
        break;
    case SLIDE_LOCATION_TOP:
        slide_geometry.y = output->geometry.y + slide_entity->offset;
        slide_geometry.height = 0;
        break;
    case SLIDE_LOCATION_BOTTOM:
        slide_geometry.y = output->geometry.y + output->geometry.height - slide_entity->offset;
        slide_geometry.height = 0;
        break;
    case SLIDE_LOCATION_RIGHT:
        slide_geometry.x = output->geometry.x + output->geometry.width - slide_entity->offset;
        slide_geometry.width = 0;
        break;
    }

    if (slide_in) {
        *start_geometry = slide_geometry;
        *end_geometry = *node_geometry;
    } else {
        *start_geometry = *node_geometry;
        *end_geometry = slide_geometry;
    }
}

static void slide_update_transform_options(struct transform_effect *effect,
                                           struct transform *transform,
                                           struct transform_options *options,
                                           struct animation_data *current, bool interrupted,
                                           void *data)
{
    struct slide_entity *slide_entity = transform_get_user_data(transform);
    if (!slide_entity->slide_in) {
        return;
    }

    if (interrupted) {
        options->start_time = current_time_msec();
    }

    struct kywc_box node_geometry;
    slide_get_node_origin_geometry(slide_entity->node, &node_geometry);
    slide_calc_start_and_end_geometry(slide_entity, &node_geometry, slide_entity->slide_in,
                                      &options->start.geometry, &options->end.geometry);
}

static void slide_get_render_src_box(struct transform_effect *effect, struct transform *transform,
                                     struct animation_data *current, struct kywc_fbox *src_fbox,
                                     void *data)
{
    struct slide_entity *slide_entity = transform_get_user_data(transform);
    /* thumbnail no scale */
    switch (slide_entity->location) {
    case SLIDE_LOCATION_LEFT:
        src_fbox->x = (src_fbox->width - current->geometry.width) < 0
                          ? 0
                          : (src_fbox->width - current->geometry.width);
        src_fbox->width = current->geometry.width;
        break;
    case SLIDE_LOCATION_RIGHT:
        src_fbox->width =
            current->geometry.width < src_fbox->width ? current->geometry.width : src_fbox->width;
        break;
    case SLIDE_LOCATION_TOP:
        src_fbox->y = src_fbox->height - current->geometry.height;
        src_fbox->height = current->geometry.height;
        break;
    case SLIDE_LOCATION_BOTTOM:
        src_fbox->height = current->geometry.height < src_fbox->height ? current->geometry.height
                                                                       : src_fbox->height;
        break;
    }
}

static void slide_effect_destroy(struct transform_effect *effect, void *data)
{
    struct slide_effect *slide_effect = data;
    free(slide_effect);
    slide_effect = NULL;
}

static void slide_handle_transform_destroy(struct wl_listener *listener, void *data)
{
    struct slide_entity *slide_entity = wl_container_of(listener, slide_entity, destroy);
    wl_list_remove(&slide_entity->destroy.link);
    free(slide_entity);
}

struct transform_effect_interface slide_impl = {
    .update_transform_options = slide_update_transform_options,
    .get_render_src_box = slide_get_render_src_box,
    .destroy = slide_effect_destroy,
};

bool slide_effect_create(struct effect_manager *manager)
{
    slide_effect = calloc(1, sizeof(*slide_effect));
    if (!slide_effect) {
        return false;
    }

    slide_effect->effect = transform_effect_create(manager, &slide_impl, "slide", 5, NULL);
    if (!slide_effect->effect) {
        free(slide_effect);
        return false;
    }

    return true;
}

static struct slide_entity *create_slide_entity(struct ky_scene_node *node, int location,
                                                int offset, bool mapped, bool is_view)
{
    struct slide_entity *slide_entity = calloc(1, sizeof(*slide_entity));
    if (!slide_entity) {
        return NULL;
    }

    slide_entity->offset = offset;
    slide_entity->location = location;
    slide_entity->slide_in = mapped;
    slide_entity->node = node;
    struct transform_options options = { 0 };
    options.start.alpha = 1.0;
    options.end.alpha = 1.0;
    options.duration = 200;
    options.type.geometry = ANIMATION_TYPE_EASE;
    options.start_time = current_time_msec();

    struct kywc_box node_geometry;
    slide_get_node_origin_geometry(slide_entity->node, &node_geometry);
    slide_calc_start_and_end_geometry(slide_entity, &node_geometry, mapped, &options.start.geometry,
                                      &options.end.geometry);

    slide_entity->transform = transform_effect_get_or_create_transform(
        slide_effect->effect, &options, slide_entity->node, slide_entity);
    if (!slide_entity->transform) {
        free(slide_entity);
        return false;
    }

    slide_entity->destroy.notify = slide_handle_transform_destroy;
    transform_add_destroy_listener(slide_entity->transform, &slide_entity->destroy);
    return slide_entity;
}

bool node_add_slide_effect(struct ky_scene_node *node, int location, int offset, bool slide_out)
{
    if (!slide_effect || !node->enabled) {
        return false;
    }

    return create_slide_entity(node, location, offset, slide_out, false);
}

bool view_add_slide_effect(struct view *view, bool mapped)
{
    if (!slide_effect) {
        return false;
    }

    if (!view->surface) {
        return false;
    }

    struct slide slide;
    if (!ky_scene_surface_get_slide(view->surface, &slide)) {
        return false;
    }

    return create_slide_entity(&view->tree->node, slide.location, slide.offset, mapped, true);
}
