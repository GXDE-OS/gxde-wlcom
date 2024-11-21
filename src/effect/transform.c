// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>
#include <string.h>

#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_compositor.h>

#include <kywc/log.h>

#include "effect/animator.h"
#include "effect/transform.h"
#include "effect_p.h"
#include "render/opengl.h"
#include "render/renderer.h"
#include "scene/scene.h"
#include "scene/surface.h"
#include "scene/thumbnail.h"
#include "util/time.h"
#include "xwayland.h"

struct transform {
    struct effect_entity *entity;
    struct transform_effect *effect;

    struct animator *animator;
    struct animation_data current;
    struct transform_options pending_options;
    struct transform_options options;
    bool interrupted;

    /* reuse transforms */
    int references;

    bool need_blur;
    struct blur_info blur_info;

    struct ky_scene_buffer *buffer;
    int node_offset_x, node_offset_y;

    struct ky_scene_buffer *zero_copy_buffer;
    struct wl_listener zero_buffer_destroy;
    struct wl_listener zero_buffer_damage;

    struct {
        struct thumbnail *thumbnail;
        /* current used in the popup */
        float scale;
        int radius[4];

        struct wlr_texture *texture;
        struct wlr_buffer *buffer;

        struct wl_listener update;
        struct wl_listener destroy;
    } thumbnail_info;

    struct ky_scene_node *node;
    struct wl_listener node_destroy;

    struct {
        struct wl_signal destroy;
    } events;

    void *user_data;
};

struct transform_effect {
    struct effect *effect;
    const struct transform_effect_interface *impl;
    struct effect_manager *manager;

    struct wlr_renderer *renderer;
    bool is_opengl_renderer;

    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;

    void *user_data;
};

static const struct effect_interface transform_effect_impl;

static bool transform_create_animator(struct transform *transform);

static struct transform *transform_create(struct transform_options *options,
                                          struct transform_effect *effect,
                                          struct ky_scene_node *node, void *data);

static bool transform_effect_entity_bounding_box(struct effect_entity *entity, struct kywc_box *box)
{
    struct transform *transform = entity->user_data;
    *box = transform->current.geometry;

    struct effect_chain *chain = entity->slot.chain;
    struct node_effect_chain *node_chain = wl_container_of(chain, node_chain, base);

    if (box->width <= 0 || box->height <= 0) {
        effect_entity_get_bounding_box(entity, BOUNDING_BOX_NO_EFFECT, box);
        return false;
    }

    int lx, ly;
    ky_scene_node_coords(node_chain->node, &lx, &ly);
    box->x -= lx;
    box->y -= ly;

    return false;
}

static bool transform_effect_frame_render_pre(struct effect_entity *entity,
                                              struct ky_scene_output *scene_output)
{
    struct transform *transform = entity->user_data;
    uint32_t time = current_time_msec();
    if (time > transform->pending_options.start_time + transform->pending_options.duration) {
        effect_entity_destroy(entity);
        return true;
    }

    struct transform_effect *effect = transform->effect;
    struct transform_options *options = &transform->options;
    /* update transform options */
    if (effect->impl->update_transform_options) {
        effect->impl->update_transform_options(effect, transform, &transform->pending_options,
                                               &transform->current, transform->interrupted,
                                               effect->user_data);
    }

    /* compare geometry, alpha, angle, time */
    if (options->start_time != transform->pending_options.start_time) {
        animator_set_time(transform->animator, transform->pending_options.start_time +
                                                   transform->pending_options.duration);
    }
    if ((!kywc_box_equal(&options->start.geometry, &transform->pending_options.start.geometry)) ||
        (options->start.alpha != transform->pending_options.start.alpha) ||
        (options->start.angle != transform->pending_options.start.angle)) {
        if (!transform_create_animator(transform)) {
            effect_entity_destroy(entity);
            return true;
        }
    } else {
        if (options->end.alpha != transform->pending_options.end.alpha) {
            animator_set_alpha(transform->animator, transform->pending_options.end.alpha);
        }
        if (options->end.angle != transform->pending_options.end.angle) {
            animator_set_angle(transform->animator, transform->pending_options.end.angle);
        }
        if (!kywc_box_equal(&options->end.geometry, &transform->pending_options.end.geometry)) {
            animator_set_position(transform->animator, transform->pending_options.end.geometry.x,
                                  transform->pending_options.end.geometry.y);
            animator_set_size(transform->animator, transform->pending_options.end.geometry.width,
                              transform->pending_options.end.geometry.height);
        }
    }

    transform->interrupted = false;
    transform->options = transform->pending_options;
    const struct animation_data *animation_data = animator_value(transform->animator, time);
    transform->current = *animation_data;

    if (effect->impl->get_render_dst_box) {
        effect->impl->get_render_dst_box(effect, transform, &transform->current.geometry,
                                         effect->user_data);
    }
    effect_entity_push_damage(entity, KY_SCENE_DAMAGE_BOTH);
    return true;
}

static void transform_effect_handle_enable(struct wl_listener *listener, void *data)
{
    struct transform_effect *effect = wl_container_of(listener, effect, enable);
    if (effect->impl->enable) {
        effect->impl->enable(effect, true, effect->user_data);
    }
}

static void transform_effect_handle_disable(struct wl_listener *listener, void *data)
{
    struct transform_effect *effect = wl_container_of(listener, effect, disable);
    if (effect->impl->enable) {
        effect->impl->enable(effect, false, effect->user_data);
    }
}

static void transform_effect_handle_destroy(struct wl_listener *listener, void *data)
{
    struct transform_effect *effect = wl_container_of(listener, effect, destroy);
    wl_list_remove(&effect->enable.link);
    wl_list_remove(&effect->disable.link);
    wl_list_remove(&effect->destroy.link);

    if (effect->impl->destroy) {
        effect->impl->destroy(effect, effect->user_data);
    }

    struct effect_entity *entity, *tmp;
    wl_list_for_each_safe(entity, tmp, &effect->effect->entities, effect_link) {
        effect_entity_destroy(entity);
    }
    free(effect);
}

struct transform_effect *transform_effect_create(struct effect_manager *manager,
                                                 struct transform_effect_interface *impl,
                                                 const char *name, int priority, void *data)
{
    struct transform_effect *transform_effect = calloc(1, sizeof(*transform_effect));
    if (!transform_effect) {
        return NULL;
    }

    transform_effect->impl = impl;
    transform_effect->user_data = data;
    transform_effect->renderer = manager->server->renderer;
    bool enabled = !ky_renderer_is_software(manager->server->renderer);
    transform_effect->is_opengl_renderer = wlr_renderer_is_opengl(transform_effect->renderer);
    struct effect *effect = effect_create(name, priority, enabled, &transform_effect_impl, NULL);
    if (!effect) {
        free(transform_effect);
        return NULL;
    }

    transform_effect->effect = effect;
    transform_effect->enable.notify = transform_effect_handle_enable;
    wl_signal_add(&transform_effect->effect->events.enable, &transform_effect->enable);
    transform_effect->disable.notify = transform_effect_handle_disable;
    wl_signal_add(&transform_effect->effect->events.disable, &transform_effect->disable);
    transform_effect->destroy.notify = transform_effect_handle_destroy;
    wl_signal_add(&transform_effect->effect->events.destroy, &transform_effect->destroy);

    return transform_effect;
}

void transform_destroy(struct transform *transform)
{
    if (!transform->entity) {
        return;
    }
    effect_entity_destroy(transform->entity);
}

void transform_set_user_data(struct transform *transform, void *data)
{
    if (!transform) {
        return;
    }
    transform->user_data = data;
}

void *transform_get_user_data(struct transform *transform)
{
    return transform->user_data;
}

void transform_block_source_update(struct transform *transform, bool block)
{
    if (!transform->thumbnail_info.thumbnail) {
        return;
    }

    thumbnail_mark_wants_update(transform->thumbnail_info.thumbnail, !block);
}

static bool transform_effect_node_push_damage(struct effect_entity *entity,
                                              struct ky_scene_node *damage_node,
                                              uint32_t *damage_type, pixman_region32_t *damage)
{
    struct kywc_box box;
    transform_effect_entity_bounding_box(entity, &box);
    pixman_region32_union_rect(damage, damage, box.x, box.y, box.width, box.height);

    struct transform *transform = entity->user_data;
    if (transform->node && transform->need_blur) {
        ky_scene_node_get_blur_info(transform->node, &transform->blur_info);
    }
    return false;
}

static void handle_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct transform *transform = wl_container_of(listener, transform, thumbnail_info.update);
    struct thumbnail_update_event *event = data;
    if (!event->buffer_changed) {
        return;
    }

    if (transform->thumbnail_info.texture) {
        wlr_texture_destroy(transform->thumbnail_info.texture);
    }

    transform->thumbnail_info.buffer = event->buffer;
    transform->thumbnail_info.texture =
        wlr_texture_from_buffer(transform->effect->renderer, event->buffer);

    if (!transform->node) {
        kywc_log(KYWC_WARN, "when thumbnail update, data node = NULL. need_blur = %d",
                 transform->need_blur);
        return;
    }

    if (transform->need_blur &&
        !thumbnail_get_node_offset(transform->thumbnail_info.thumbnail, transform->node,
                                   &transform->node_offset_x, &transform->node_offset_y)) {
        kywc_log(KYWC_WARN, "when thumbnail update, thumbnail get node offset failed.");
    }
}

static void handle_texture_update(struct wl_listener *listener, void *data)
{
    struct transform *transform = wl_container_of(listener, transform, zero_buffer_damage);
    if (transform->thumbnail_info.texture) {
        wlr_texture_destroy(transform->thumbnail_info.texture);
    }

    transform->thumbnail_info.buffer = NULL;
    transform->thumbnail_info.texture = NULL;
    if (transform->zero_copy_buffer->buffer) {
        transform->thumbnail_info.buffer = transform->zero_copy_buffer->buffer;
        transform->thumbnail_info.texture = wlr_texture_from_buffer(
            transform->effect->renderer, transform->zero_copy_buffer->buffer);
    }
}

static void handle_thumbnail_destroy(struct wl_listener *listener, void *data)
{
    struct transform *transform = wl_container_of(listener, transform, thumbnail_info.destroy);
    wl_list_remove(&transform->thumbnail_info.destroy.link);
    wl_list_remove(&transform->thumbnail_info.update.link);

    transform->thumbnail_info.thumbnail = NULL;
}

static void handle_zero_buffer_destroy(struct wl_listener *listener, void *data)
{
    struct transform *transform = wl_container_of(listener, transform, zero_buffer_destroy);
    wl_list_remove(&transform->zero_buffer_destroy.link);
    wl_list_remove(&transform->zero_buffer_damage.link);

    transform->zero_copy_buffer = NULL;
}

static bool transform_effect_frame_render_post(struct effect_entity *entity,
                                               struct ky_scene_render_target *target)
{
    effect_entity_push_damage(entity, KY_SCENE_DAMAGE_BOTH);
    return true;
}

static bool handle_transform_effect_configure(struct effect *effect,
                                              const struct effect_option *option)
{
    if (effect_option_is_enabled_option(option)) {
        return true;
    }

    return false;
}

static void transform_do_destroy(struct transform *transform)
{
    transform->references--;
    if (transform->references != 0) {
        return;
    }

    if (transform->zero_copy_buffer) {
        wl_list_remove(&transform->zero_buffer_destroy.link);
        wl_list_remove(&transform->zero_buffer_damage.link);
    }

    wl_signal_emit_mutable(&transform->events.destroy, transform);

    if (transform->buffer) {
        ky_scene_node_destroy(&transform->buffer->node);
    }
    if (transform->thumbnail_info.thumbnail) {
        wl_list_remove(&transform->thumbnail_info.update.link);
        wl_list_remove(&transform->thumbnail_info.destroy.link);
        thumbnail_destroy(transform->thumbnail_info.thumbnail);
    }
    if (transform->thumbnail_info.texture) {
        wlr_texture_destroy(transform->thumbnail_info.texture);
    }
    if (transform->animator) {
        animator_destroy(transform->animator);
    }

    pixman_region32_fini(&transform->blur_info.region);
    wl_list_remove(&transform->node_destroy.link);
    free(transform);
}

static void transform_effect_entity_destroy(struct effect_entity *entity)
{
    struct transform *transform = entity->user_data;
    if (!transform) {
        return;
    }

    transform_do_destroy(transform);
}

static bool transform_effect_node_render(struct effect_entity *entity, int lx, int ly,
                                         struct ky_scene_render_target *target)
{
    struct transform *transform = entity->user_data;
    if (!target->output || !transform->thumbnail_info.texture) {
        return false;
    }

    struct kywc_fbox src_box = { 0, 0, transform->thumbnail_info.texture->width,
                                 transform->thumbnail_info.texture->height };
    if (transform->effect->impl->get_render_src_box) {
        transform->effect->impl->get_render_src_box(transform->effect, transform,
                                                    &transform->current, &src_box,
                                                    transform->effect->user_data);
    }

    /* if data need blur is false, blur region is empty */
    bool need_blur = pixman_region32_not_empty(&transform->blur_info.region);

    struct ky_scene_node *zero_node = NULL;
    int radius[4] = { 0 };
    if (transform->zero_copy_buffer) {
        zero_node = &transform->zero_copy_buffer->node;
        radius[0] = zero_node->radius[0];
        radius[1] = zero_node->radius[1];
        radius[2] = zero_node->radius[2];
        radius[3] = zero_node->radius[3];
    }

    const struct ky_scene_render_texture_options opts = {
        .texture = transform->thumbnail_info.texture,
        .geometry_box = &transform->current.geometry,
        .alpha = &transform->current.alpha,
        .src = &src_box,
        .radius = &radius,
        .blur ={
            .alpha = &transform->current.alpha,
            .info = need_blur ? &transform->blur_info : NULL,
            .offset_x = transform->node_offset_x,
            .offset_y = transform->node_offset_y,
            .radius = &transform->thumbnail_info.radius,
            .scale = &transform->thumbnail_info.scale,
        },
    };
    ky_scene_render_target_add_texture(target, &opts);

    return false;
}

static const struct effect_interface transform_effect_impl = {
    .entity_destroy = transform_effect_entity_destroy,
    .entity_bounding_box = transform_effect_entity_bounding_box,
    .node_push_damage = transform_effect_node_push_damage,
    .node_render = transform_effect_node_render,
    .frame_render_pre = transform_effect_frame_render_pre,
    .frame_render_post = transform_effect_frame_render_post,
    .configure = handle_transform_effect_configure,
};

static void transform_handle_buffer_node_destroy(struct wl_listener *listener, void *data)
{
    struct transform *transform = wl_container_of(listener, transform, node_destroy);

    /**
     * node destroy before effect entity destroy.
     * so when transform destroy, node_destroy already remove.
     */
    wl_list_remove(&transform->node_destroy.link);
    wl_list_init(&transform->node_destroy.link);
    transform->buffer = NULL;
}

static bool transform_create_animator(struct transform *transform)
{
    struct transform_options *options = &transform->pending_options;
    struct animator *animator = animator_create(&options->start, options->type, options->start_time,
                                                options->start_time + options->duration);
    if (!animator) {
        return false;
    }

    if (transform->animator) {
        animator_destroy(transform->animator);
    }
    transform->animator = animator;

    animator_set_position(transform->animator, options->end.geometry.x, options->end.geometry.y);
    animator_set_size(transform->animator, options->end.geometry.width,
                      options->end.geometry.height);
    animator_set_alpha(transform->animator, options->end.alpha);
    animator_set_angle(transform->animator, options->end.angle);
    return true;
}

static struct transform *transform_create(struct transform_options *options,
                                          struct transform_effect *effect,
                                          struct ky_scene_node *node, void *data)
{
    struct transform *transform = calloc(1, sizeof(*transform));
    if (!transform) {
        return NULL;
    }

    transform->buffer = NULL;
    transform->effect = effect;
    transform->node = node;
    transform->pending_options = *options;
    transform->options = *options;
    transform->user_data = data;
    transform->references = 1;
    transform->interrupted = false;
    transform->zero_copy_buffer = options->buffer;

    wl_signal_init(&transform->events.destroy);
    wl_list_init(&transform->zero_buffer_damage.link);
    wl_list_init(&transform->zero_buffer_destroy.link);
    wl_list_init(&transform->node_destroy.link);

    pixman_region32_init(&transform->blur_info.region);
    transform->need_blur = effect->is_opengl_renderer;

    if (!transform_create_animator(transform)) {
        transform_do_destroy(transform);
        return NULL;
    }

    if (transform->need_blur) {
        ky_scene_node_get_blur_info(transform->node, &transform->blur_info);
        ky_scene_node_get_radius(transform->node, transform->thumbnail_info.radius);
    }

    transform->thumbnail_info.scale = options->scale <= 0 ? 1.0f : options->scale;

    if (transform->zero_copy_buffer) {
        transform->thumbnail_info.buffer = transform->zero_copy_buffer->buffer;
        transform->thumbnail_info.texture = transform->zero_copy_buffer->texture;
        if (!transform->thumbnail_info.texture) {
            transform->thumbnail_info.texture = wlr_texture_from_buffer(
                transform->effect->renderer, transform->zero_copy_buffer->buffer);
        }

        transform->zero_buffer_damage.notify = handle_texture_update;
        wl_signal_add(&transform->zero_copy_buffer->node.events.damage,
                      &transform->zero_buffer_damage);

        transform->zero_buffer_destroy.notify = handle_zero_buffer_destroy;
        wl_signal_add(&transform->zero_copy_buffer->node.events.destroy,
                      &transform->zero_buffer_destroy);

        return transform;
    }

    transform->thumbnail_info.thumbnail =
        thumbnail_create_from_node(node, transform->thumbnail_info.scale);
    if (!transform->thumbnail_info.thumbnail) {
        transform_do_destroy(transform);
        return NULL;
    }

    transform->thumbnail_info.update.notify = handle_thumbnail_update;
    thumbnail_add_update_listener(transform->thumbnail_info.thumbnail,
                                  &transform->thumbnail_info.update);
    transform->thumbnail_info.destroy.notify = handle_thumbnail_destroy;
    thumbnail_add_destroy_listener(transform->thumbnail_info.thumbnail,
                                   &transform->thumbnail_info.destroy);

    thumbnail_update(transform->thumbnail_info.thumbnail);
    if (!transform->thumbnail_info.buffer) {
        transform_do_destroy(transform);
        return NULL;
    }

    return transform;
}

static void transform_handle_node_destroy(struct wl_listener *listener, void *data)
{
    struct transform *transform = wl_container_of(listener, transform, node_destroy);
    /**
     * node destroy before effect entity destroy.
     * so when transform data destroy, node_destroy already remove.
     */
    wl_list_remove(&transform->node_destroy.link);
    wl_list_init(&transform->node_destroy.link);
    if (!transform->thumbnail_info.buffer) {
        return;
    }

    /**
     * when node destroy, if visible is empty, there is no need to render, not add effect.
     * when node was just created, it was never rendered, visible is also empty, but it needs to
     * render and add effect.
     */
    struct effect_entity *_entity = transform->entity;
    struct node_effect_chain *node_chain = wl_container_of(_entity->slot.chain, node_chain, base);
    if (!pixman_region32_not_empty(&node_chain->visible_region)) {
        return;
    }

    struct ky_scene_tree *layer_tree = view_manager_get_layer_tree(transform->node);
    if (!layer_tree) {
        kywc_log(KYWC_ERROR, "node is not in layer");
        return;
    }

    struct ky_scene_node *sibing = transform->node;
    struct ky_scene_tree *parent = sibing->parent;
    while (parent != layer_tree) {
        sibing = &parent->node;
        parent = parent->node.parent;
    }

    struct ky_scene_buffer *buffer =
        ky_scene_buffer_create(parent, transform->thumbnail_info.buffer);
    if (!buffer) {
        return;
    }

    transform->buffer = buffer;
    ky_scene_node_place_above(&buffer->node, sibing);
    ky_scene_node_set_input_bypassed(&buffer->node, true);

    struct effect_entity *entity =
        ky_scene_node_add_effect(&buffer->node, transform->effect->effect);
    if (!entity) {
        animator_destroy(transform->animator);
        ky_scene_node_destroy(&buffer->node);
        return;
    }
    /**
     * in general, transform is used to modify node. When destroy signal is triggered, the proxy
     * node method is used. transform is used to modify buffer node
     */
    transform->node = &buffer->node;

    transform->zero_copy_buffer = NULL;

    entity->user_data = transform;
    transform->references++;

    transform->node_destroy.notify = transform_handle_buffer_node_destroy;
    wl_signal_add(&buffer->node.events.destroy, &transform->node_destroy);
}

struct ky_scene_buffer *transform_get_zero_copy_buffer(struct view *view)
{
    if (!xwayland_check_view(view) || view->base.ssd != KYWC_SSD_NONE) {
        return NULL;
    }

    struct ky_scene_buffer *buffer = ky_scene_buffer_try_from_surface(view->surface);
    if (!buffer) {
        return NULL;
    }

    if (pixman_region32_not_empty(&buffer->node.clip_region) || !wlr_fbox_empty(&buffer->src_box)) {
        return NULL;
    }

    return buffer;
}

void transform_add_destroy_listener(struct transform *transform, struct wl_listener *listener)
{
    wl_signal_add(&transform->events.destroy, listener);
}

struct transform *transform_effect_get_or_create_transform(struct transform_effect *effect,
                                                           struct transform_options *options,
                                                           struct ky_scene_node *node, void *data)
{
    if (!effect->effect->enabled) {
        return NULL;
    }

    struct effect_entity *entity = ky_scene_node_find_effect_entity(node, effect->effect);
    /* current effect interrupt previous effect, reuse same entity and transform */
    if (entity) {
        struct transform *transform = entity->user_data;
        transform->options = transform->pending_options;
        transform->pending_options.end = options->end;
        transform->interrupted = true;
        /* update thumbnail immediately when effect interrupted */
        transform_block_source_update(transform, false);
        if (transform->thumbnail_info.thumbnail) {
            thumbnail_update(transform->thumbnail_info.thumbnail);
        }
        /* new data to transform */
        transform->user_data = data;
        return transform;
    }

    struct transform *transform = transform_create(options, effect, node, data);
    if (!transform) {
        return NULL;
    }

    entity = ky_scene_node_add_effect(node, effect->effect);
    if (!entity) {
        transform_do_destroy(transform);
        return NULL;
    }

    transform->node_destroy.notify = transform_handle_node_destroy;
    wl_signal_add(&node->events.destroy, &transform->node_destroy);
    transform->entity = entity;
    entity->user_data = transform;

    return transform;
}
