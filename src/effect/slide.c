// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include <wlr/util/box.h>

#include <kywc/log.h>

#include "effect/animator.h"
#include "effect/slide.h"
#include "effect_p.h"
#include "scene/surface.h"
#include "scene/thumbnail.h"

#include "output.h"
#include "render/opengl.h"
#include "theme.h"
#include "util/time.h"

struct slide_data {
    struct ky_scene_node *node;

    struct ky_scene_buffer *buffer;

    struct slide slide;
    bool slide_out;

    int64_t start_time;
    int duration;

    struct kywc_box start_geometry;
    struct kywc_box end_geometry;

    struct animator *animator;
    struct animation_data current;

    struct thumbnail *node_thumbnail;
    struct wlr_buffer *thumbnail_buffer;
    struct wlr_texture *thumbnail_texture;

    struct wl_listener thumbnail_update;
    struct wl_listener thumbnail_destroy;
    /* listen node or scene buffer destroy */
    struct wl_listener node_destroy;
};

struct slide_effect {
    struct effect *effect;

    int duration;
    struct ky_scene *scene;
    struct wlr_renderer *renderer;

    struct wl_listener destroy;
};

struct slide_effect *slide_effect = NULL;

static void slide_calc_start_and_end_geometry(struct slide_data *data,
                                              struct kywc_box *node_geometry)
{
    double center_x = node_geometry->x + node_geometry->width / 2.0;
    double center_y = node_geometry->y + node_geometry->height / 2.0;
    struct kywc_output *kywc_output = kywc_output_at_point(center_x, center_y);
    struct output *output = output_from_kywc_output(kywc_output);

    struct kywc_box slide_geometry = *node_geometry;
    switch (data->slide.location) {
    case SLIDE_LOCATION_LEFT:
        slide_geometry.x = output->geometry.x + data->slide.offset;
        slide_geometry.width = 0;
        break;
    case SLIDE_LOCATION_TOP:
        slide_geometry.y = output->geometry.y + data->slide.offset;
        slide_geometry.height = 0;
        break;
    case SLIDE_LOCATION_BOTTOM:
        slide_geometry.y = output->geometry.y + output->geometry.height - data->slide.offset;
        slide_geometry.height = 0;
        break;
    case SLIDE_LOCATION_RIGHT:
        slide_geometry.x = output->geometry.x + output->geometry.width - data->slide.offset;
        slide_geometry.width = 0;
        break;
    }

    if (data->slide_out) {
        data->start_geometry = slide_geometry;
        data->end_geometry = *node_geometry;
    } else {
        data->start_geometry = *node_geometry;
        data->end_geometry = slide_geometry;
    }
}

static void get_node_origin_box(struct effect_entity *entity, struct kywc_box *layout_box)
{
    struct effect_chain *chain = entity->slot.chain;
    if (!chain) {
        *layout_box = (struct kywc_box){ 0 };
        return;
    }

    struct wlr_box box;
    struct node_effect_chain *node_chain = wl_container_of(chain, node_chain, base);
    node_chain->impl.get_bounding_box(node_chain->node, &box);
    ky_scene_node_coords(node_chain->node, &layout_box->x, &layout_box->y);

    layout_box->x += box.x;
    layout_box->y += box.y;
    layout_box->width = box.width;
    layout_box->height = box.height;
}

static void handle_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct slide_data *slide_data = wl_container_of(listener, slide_data, thumbnail_update);

    struct thumbnail_update_event *event = data;
    if (!event->buffer_changed) {
        return;
    }

    if (slide_data->thumbnail_texture) {
        wlr_texture_destroy(slide_data->thumbnail_texture);
    }
    slide_data->thumbnail_buffer = event->buffer;
    slide_data->thumbnail_texture = wlr_texture_from_buffer(slide_effect->renderer, event->buffer);

    thumbnail_mark_wants_update(slide_data->node_thumbnail, false);
}

static void handle_thumbnail_destroy(struct wl_listener *listener, void *data)
{
    struct slide_data *slide_data = wl_container_of(listener, slide_data, thumbnail_destroy);
    wl_list_remove(&slide_data->thumbnail_destroy.link);
    wl_list_remove(&slide_data->thumbnail_update.link);

    slide_data->node_thumbnail = NULL;
}

static void slide_get_src_box(struct slide_data *data, struct wlr_fbox *src_fbox)
{
    src_fbox->x = 0, src_fbox->y = 0;
    src_fbox->width = data->thumbnail_texture->width;
    src_fbox->height = data->thumbnail_texture->height;

    /* thumbnail no scale*/
    switch (data->slide.location) {
    case SLIDE_LOCATION_LEFT:
        src_fbox->x = src_fbox->width - data->current.geometry.width;
        src_fbox->width = src_fbox->x < 0 ? 0 : data->current.geometry.width;
        src_fbox->x = src_fbox->x < 0 ? 0 : src_fbox->x;
        break;
    case SLIDE_LOCATION_RIGHT:
        src_fbox->width = data->current.geometry.width < src_fbox->width
                              ? data->current.geometry.width
                              : src_fbox->width;
        break;
    case SLIDE_LOCATION_TOP:
        data->start_geometry.y = src_fbox->height - data->current.geometry.height;
        src_fbox->height = src_fbox->y < 0 ? 0 : data->current.geometry.height;
        src_fbox->y = src_fbox->y < 0 ? 0 : src_fbox->y;
        break;
    case SLIDE_LOCATION_BOTTOM:
        src_fbox->height = data->current.geometry.height < src_fbox->height
                               ? data->current.geometry.height
                               : src_fbox->height;
        break;
    }
}

static void slide_data_destroy(struct slide_data *data)
{
    if (data->buffer) {
        ky_scene_node_destroy(&data->buffer->node);
    }
    if (data->node_thumbnail) {
        wl_list_remove(&data->thumbnail_destroy.link);
        wl_list_remove(&data->thumbnail_update.link);
        thumbnail_destroy(data->node_thumbnail);
    }
    if (data->thumbnail_texture) {
        wlr_texture_destroy(data->thumbnail_texture);
    }
    if (data->animator) {
        animator_destroy(data->animator);
    }

    wl_list_remove(&data->node_destroy.link);
    free(data);
}

static void slide_entity_push_damage(struct effect_entity *entity)
{
    struct effect_chain *chain = entity->slot.chain;
    struct node_effect_chain *node_chain = wl_container_of(chain, node_chain, base);
    ky_scene_node_push_damage(node_chain->node, KY_SCENE_DAMAGE_BOTH, NULL);
}

static void slide_entity_destroy(struct effect_entity *entity)
{
    struct slide_data *data = entity->user_data;
    slide_data_destroy(data);
}

static bool slide_entity_bounding_box(struct effect_entity *entity, struct kywc_box *box)
{
    /* effect entity must on node, couldn't be NULL */
    struct effect_chain *chain = entity->slot.chain;
    struct node_effect_chain *node_chain = wl_container_of(chain, node_chain, base);

    struct slide_data *data = entity->user_data;
    *box = data->current.geometry;
    if (box->width <= 0 || box->height <= 0) {
        struct wlr_box node_box;
        node_chain->impl.get_bounding_box(node_chain->node, &node_box);
        box->x = node_box.x;
        box->y = node_box.y;
        box->width = node_box.width;
        box->height = node_box.height;
        return false;
    }

    int lx, ly;
    ky_scene_node_coords(node_chain->node, &lx, &ly);
    box->x -= lx;
    box->y -= ly;
    return false;
}

static bool slide_node_push_damage(struct effect_entity *entity, struct ky_scene_node *damage_node,
                                   uint32_t *damage_type, pixman_region32_t *damage)
{
    struct kywc_box box;
    slide_entity_bounding_box(entity, &box);
    pixman_region32_union_rect(damage, damage, box.x, box.y, box.width, box.height);
    return true;
}

static bool slide_node_render(struct effect_entity *entity, int lx, int ly,
                              struct ky_scene_render_target *target)
{
    struct slide_data *data = entity->user_data;
    if (!data->thumbnail_texture) {
        return false;
    }

    struct animation_data *current = &data->current;
    struct kywc_box geometry = data->current.geometry;
    pixman_region32_t render_region;
    pixman_region32_init(&render_region);
    pixman_region32_intersect_rect(&render_region, &target->damage, geometry.x, geometry.y,
                                   geometry.width, geometry.height);
    if (!pixman_region32_not_empty(&render_region)) {
        pixman_region32_fini(&render_region);
        return false;
    }

    struct wlr_fbox src_box;
    slide_get_src_box(data, &src_box);

    struct wlr_box dst_box = {
        .x = geometry.x - target->logical.x,
        .y = geometry.y - target->logical.y,
        .width = geometry.width,
        .height = geometry.height,
    };
    ky_scene_render_box(&dst_box, target);

    pixman_region32_translate(&render_region, -target->logical.x, -target->logical.y);
    ky_scene_render_region(&render_region, target);

    struct ky_render_texture_options options = {
        .base = {
            .texture = data->thumbnail_texture,
            .src_box = src_box,
            .dst_box = dst_box,
            .alpha = &current->alpha,
            .transform = target->transform,
            .clip = &render_region,
            .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
        },
        .rotation_angle = current->angle,
    };
    ky_render_pass_add_texture(target->render_pass, &options);

    pixman_region32_fini(&render_region);
    return false;
}

static bool slide_data_create_animation(struct slide_data *data)
{
    data->current.alpha = 1;
    data->current.angle = 0;
    data->current.geometry = data->start_geometry;
    struct animation_type_group type = {
        .geometry = ANIMATION_TYPE_EASE,
    };
    data->animator =
        animator_create(&data->current, type, data->start_time, data->start_time + data->duration);
    if (!data->animator) {
        return false;
    }

    animator_set_position(data->animator, data->end_geometry.x, data->end_geometry.y);
    animator_set_size(data->animator, data->end_geometry.width, data->end_geometry.height);
    return true;
}

static bool slide_frame_render_pre(struct effect_entity *entity,
                                   struct ky_scene_output *scene_output)
{
    struct slide_data *data = entity->user_data;
    int64_t time = current_time_msec();
    if (time > data->start_time + data->duration) {
        effect_entity_destroy(entity);
        return true;
    }

    if (data->slide_out) {
        struct kywc_box layout_box;
        get_node_origin_box(entity, &layout_box);

        if (!kywc_box_equal(&data->end_geometry, &layout_box)) {
            slide_calc_start_and_end_geometry(data, &layout_box);

            if (data->animator) {
                animator_destroy(data->animator);
            }
            if (!slide_data_create_animation(data)) {
                effect_entity_destroy(entity);
                return true;
            }
        }
    }

    if (data->buffer) {
        ky_scene_node_raise_to_top(&data->buffer->node);
    }
    const struct animation_data *animation_data = animator_value(data->animator, time);
    data->current = *animation_data;

    slide_entity_push_damage(entity);
    return true;
}

static bool slide_frame_render_post(struct effect_entity *entity,
                                    struct ky_scene_render_target *target)
{
    slide_entity_push_damage(entity);
    return true;
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct effect_entity *entity, *tmp;
    wl_list_for_each_safe(entity, tmp, &slide_effect->effect->entities, effect_link) {
        effect_entity_destroy(entity);
    }

    wl_list_remove(&slide_effect->destroy.link);
    free(slide_effect);
    slide_effect = NULL;
}

static const struct effect_interface slide_effect_impl = {
    .entity_destroy = slide_entity_destroy,
    .entity_bounding_box = slide_entity_bounding_box,
    .node_push_damage = slide_node_push_damage,
    .node_render = slide_node_render,
    .frame_render_pre = slide_frame_render_pre,
    .frame_render_post = slide_frame_render_post,
};

bool slide_effect_create(struct effect_manager *manager)
{
    slide_effect = calloc(1, sizeof(*slide_effect));
    if (!slide_effect) {
        return false;
    }

    bool enabled = wlr_renderer_is_opengl(manager->server->renderer);
    slide_effect->effect = effect_create("slide", 5, enabled, &slide_effect_impl);
    if (!slide_effect->effect) {
        free(slide_effect);
        slide_effect = NULL;
        return false;
    }

    slide_effect->duration = 300;
    slide_effect->scene = manager->server->scene;
    slide_effect->renderer = manager->server->renderer;

    slide_effect->destroy.notify = handle_effect_destroy;
    wl_signal_add(&slide_effect->effect->events.destroy, &slide_effect->destroy);

    return true;
}

static struct slide_data *slide_data_create(struct ky_scene_node *node, const struct slide *slide,
                                            bool slide_out)
{
    struct slide_data *data = calloc(1, sizeof(*data));
    if (!data) {
        return NULL;
    }

    data->node = node;
    data->buffer = NULL;
    data->slide = *slide;
    data->slide_out = slide_out;
    data->start_time = current_time_msec();
    data->duration = slide_effect->duration; // ms

    data->node_thumbnail = thumbnail_create_from_node(node, 1.0f);
    if (!data->node_thumbnail) {
        free(data);
        return NULL;
    }

    data->thumbnail_update.notify = handle_thumbnail_update;
    thumbnail_add_update_listener(data->node_thumbnail, &data->thumbnail_update);
    data->thumbnail_destroy.notify = handle_thumbnail_destroy;
    thumbnail_add_destroy_listener(data->node_thumbnail, &data->thumbnail_destroy);

    if (!slide_out) {
        thumbnail_update(data->node_thumbnail);
    }

    struct wlr_box box;
    struct kywc_box layout_box;
    node->impl.get_bounding_box(node, &box);
    ky_scene_node_coords(node, &layout_box.x, &layout_box.y);
    layout_box.x += box.x;
    layout_box.y += box.y;
    layout_box.width = box.width;
    layout_box.height = box.height;

    slide_calc_start_and_end_geometry(data, &layout_box);

    if (!slide_data_create_animation(data)) {
        wl_list_remove(&data->thumbnail_destroy.link);
        wl_list_remove(&data->thumbnail_update.link);
        thumbnail_destroy(data->node_thumbnail);
        free(data);
        return NULL;
    }

    return data;
}

static struct ky_scene_tree *get_node_layer_tree(struct ky_scene_node *node)
{
    struct ky_scene_tree *tree = node->parent;

    int deep = 0;
    while (tree) {
        deep++;
        tree = tree->node.parent;
    }

    struct ky_scene_tree *view_parent = node->parent;
    view_parent = deep >= 4 ? view_parent->node.parent : view_parent;

    return view_parent;
}

static void slide_data_handle_buffer_node_destroy(struct wl_listener *listener, void *data)
{
    struct slide_data *slide_data = wl_container_of(listener, slide_data, node_destroy);

    /**
     * node destroy before effect entity destroy.
     * so when slide data destroy, node_destroy already remove.
     */
    wl_list_remove(&slide_data->node_destroy.link);
    wl_list_init(&slide_data->node_destroy.link);

    slide_data->buffer = NULL;
}

static void slide_data_handle_node_destroy(struct wl_listener *listener, void *data)
{
    struct slide_data *slide_data = wl_container_of(listener, slide_data, node_destroy);

    /**
     * node destroy before effect entity destroy.
     * so when slide data destroy, node_destroy already remove.
     */
    wl_list_remove(&slide_data->node_destroy.link);
    wl_list_init(&slide_data->node_destroy.link);

    if (slide_data->slide_out || !slide_data->thumbnail_buffer) {
        return;
    }

    struct ky_scene_tree *layer_tree = get_node_layer_tree(slide_data->node);
    if (!layer_tree) {
        kywc_log(KYWC_WARN, "node don't in layer");
    }

    struct ky_scene_buffer *buffer =
        ky_scene_buffer_create(layer_tree, slide_data->thumbnail_buffer);
    if (!buffer) {
        return;
    }

    struct effect_entity *entity = ky_scene_node_add_effect(&buffer->node, slide_effect->effect);
    if (!entity) {
        ky_scene_node_destroy(&buffer->node);
        return;
    }

    struct slide_data *new_data = calloc(1, sizeof(*new_data));
    if (!new_data) {
        ky_scene_node_destroy(&buffer->node);
        return;
    }

    new_data->node = NULL;
    new_data->slide = slide_data->slide;
    new_data->slide_out = slide_data->slide_out;
    new_data->start_time = slide_data->start_time;
    new_data->duration = slide_data->duration; // ms

    new_data->thumbnail_texture = slide_data->thumbnail_texture;
    new_data->thumbnail_buffer = slide_data->thumbnail_buffer;
    new_data->node_thumbnail = NULL;

    new_data->start_geometry = slide_data->start_geometry;
    new_data->end_geometry = slide_data->end_geometry;

    new_data->current = slide_data->current;
    new_data->animator = slide_data->animator;

    new_data->buffer = buffer;
    new_data->node_destroy.notify = slide_data_handle_buffer_node_destroy;
    wl_signal_add(&buffer->node.events.destroy, &new_data->node_destroy);

    entity->user_data = new_data;

    /* must be NULL, otherwise slide data will destroy it when effect entity destroy */
    slide_data->animator = NULL;
    slide_data->thumbnail_texture = NULL;
}

bool node_add_slide_effect(struct ky_scene_node *node, int location, int offset, bool slid_out)
{
    if (!slide_effect || !slide_effect->effect->enabled || !node->enabled) {
        return false;
    }

    struct slide_data *data = NULL;
    struct effect_entity *entity = ky_scene_node_find_effect_entity(node, slide_effect->effect);
    if (entity) {
        data = entity->user_data;
        data->slide_out = slid_out;
        data->start_time = current_time_msec();

        struct kywc_box box;
        /* get node origin box, don't affecte by effect */
        get_node_origin_box(entity, &box);
        slide_calc_start_and_end_geometry(data, &box);
        animator_set_time(data->animator, data->start_time + data->duration);
        animator_set_position(data->animator, data->end_geometry.x, data->end_geometry.y);
        animator_set_size(data->animator, data->end_geometry.width, data->end_geometry.height);
        return true;
    }

    struct slide slide = { .location = location, .offset = offset };
    data = slide_data_create(node, &slide, slid_out);
    if (!data) {
        return false;
    }

    entity = ky_scene_node_add_effect(node, slide_effect->effect);
    if (!entity) {
        slide_data_destroy(data);
        return false;
    }

    data->node_destroy.notify = slide_data_handle_node_destroy;
    wl_signal_add(&node->events.destroy, &data->node_destroy);

    entity->user_data = data;
    return true;
}

bool view_add_slide_effect(struct view *view, bool mapped)
{
    if (!view->surface) {
        return false;
    }

    struct slide slide;
    if (!ky_scene_surface_get_slide(view->surface, &slide)) {
        return false;
    }

    return node_add_slide_effect(&view->tree->node, slide.location, slide.offset, mapped);
}
