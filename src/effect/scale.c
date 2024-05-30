// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>
#include <string.h>

#include "effect/animator.h"
#include "effect/scale.h"
#include "effect_p.h"
#include "output.h"
#include "render/opengl.h"
#include "scene/thumbnail.h"
#include "theme.h"
#include "util/time.h"

struct padding {
    int top, bottom, left, right;
};

struct scale_effect_data {
    struct animator *animator;
    struct animation_data current;

    struct kywc_box start_geometry;
    struct kywc_box end_geometry;
    struct kywc_box render_box;
    float start_alpha, end_alpha;
    int64_t start_time;

    enum scale_action action;

    struct ky_scene_node *node;
    struct view *view;
    struct wl_listener view_size;
    struct wl_listener view_position;

    struct thumbnail *thumbnail;
    struct wlr_texture *thumbnail_texture;
    struct wl_listener thumbnail_update;
    struct wl_listener thumbnail_destroy;
};

struct scale_effect {
    struct effect *effect;
    struct wl_listener destroy;

    struct effect_manager *manager;
    int duration;
};

static struct scale_effect *scale = NULL;

static void calc_view_box(struct kywc_view *view, struct kywc_box *geometry_box,
                          struct kywc_box *box)
{
    box->x = geometry_box->x - view->margin.off_x;
    box->y = geometry_box->y - view->margin.off_y;
    box->width = geometry_box->width + view->margin.off_width;
    box->height = geometry_box->height + view->margin.off_height;
}

static void scale_calc_shadow(struct kywc_view *view, struct padding *shadow)
{
    struct theme *theme = theme_manager_get_current();
    if (view->ssd == KYWC_SSD_NONE) {
        memcpy(shadow, &view->padding, sizeof(struct padding));
    } else {
        shadow->top = shadow->bottom = theme->shadow_border;
        shadow->left = shadow->right = theme->shadow_border;
    }
}

static void scale_calc_start_and_end_geometry(struct scale_effect_data *data)
{
    struct view *view = data->view;
    struct kywc_view *kywc_view = &view->base;

    /* calc maximize start and end geometry */
    if (data->action == SCALE_MAXIMIZE) {
        if (current_time_msec() > data->start_time + scale->duration) {
            calc_view_box(kywc_view, &view->pending.geometry, &data->start_geometry);
        }
        calc_view_box(kywc_view, &kywc_view->geometry, &data->end_geometry);
    } else {
        /* calc minimize start and end geometry */
        struct kywc_box end_box = { 0, 0, 10, 10 };
        if (view_manager_get_show_desktop()) {
            struct output *output = output_from_kywc_output(view->output);
            end_box.x = (output->geometry.width - end_box.width) / 2;
            end_box.y = (output->geometry.height - end_box.height) / 2;
        } else {
            end_box.x = kywc_view->geometry.x + kywc_view->geometry.width / 2;
            end_box.y = kywc_view->geometry.y + kywc_view->geometry.height / 2;
        }

        if (current_time_msec() > data->start_time + scale->duration) {
            struct kywc_box start_geometry = kywc_view->minimized ? kywc_view->geometry : end_box;
            calc_view_box(kywc_view, &start_geometry, &data->start_geometry);
        }
        struct kywc_box end_geometry = kywc_view->minimized ? end_box : kywc_view->geometry;
        calc_view_box(kywc_view, &end_geometry, &data->end_geometry);
    }

    /* calc start geometry when scale effect interrupted */
    if (current_time_msec() <= data->start_time + scale->duration) {
        data->start_geometry = data->current.geometry;
    }
}

static void scale_calc_render_box(struct scale_effect_data *data, struct padding *padding)
{
    struct kywc_view *view = &data->view->base;
    /* shadow */
    struct kywc_box *geometry = &data->current.geometry;
    float width_scale, height_scale;
    width_scale = 1.0 * geometry->width / (view->geometry.width + view->margin.off_width);
    height_scale = 1.0 * geometry->height / (view->geometry.height + view->margin.off_height);
    struct padding shadow = {
        .left = width_scale * padding->left,
        .right = width_scale * padding->right,
        .top = ceil(height_scale * padding->top),
        .bottom = ceil(height_scale * padding->bottom),
    };

    /* render box */
    struct kywc_box *render_box = &data->render_box;
    render_box->x = geometry->x - shadow.left;
    render_box->y = geometry->y - shadow.right;
    render_box->width = geometry->width + shadow.top + shadow.left;
    render_box->height = geometry->height + shadow.bottom + shadow.right;
}

static void scale_entity_destroy(struct effect_entity *entity)
{
    struct scale_effect_data *data = entity->user_data;
    if (data->thumbnail) {
        wl_list_remove(&data->thumbnail_update.link);
        wl_list_remove(&data->thumbnail_destroy.link);
        thumbnail_destroy(data->thumbnail);
    }
    if (data->thumbnail_texture) {
        wlr_texture_destroy(data->thumbnail_texture);
    }
    if (data->animator) {
        animator_destroy(data->animator);
    }

    struct view *view = data->view;
    if (view->base.minimized) {
        ky_scene_node_set_enabled(&view->tree->node, false);
        view->current_proxy ? ky_scene_node_lower_to_bottom(&view->current_proxy->tree->node)
                            : ky_scene_node_lower_to_bottom(&view->tree->node);
    }

    wl_list_remove(&data->view_position.link);
    wl_list_remove(&data->view_size.link);
    free(data);
}

static bool scale_node_render(struct effect_entity *entity, int lx, int ly,
                              struct ky_scene_render_target *target)
{
    struct scale_effect_data *data = entity->user_data;
    if (!target->output || !data->thumbnail_texture) {
        return false;
    }

    struct padding shadow = { 0 };
    scale_calc_shadow(&data->view->base, &shadow);
    scale_calc_render_box(data, &shadow);
    data->current.geometry = data->render_box;
    animator_render_texture(&data->current, target, data->thumbnail_texture);

    return false;
}

static void scale_init_alpha_and_geometry(struct scale_effect_data *scale_data)
{
    struct kywc_view *view = &scale_data->view->base;
    scale_calc_start_and_end_geometry(scale_data);
    if (scale_data->action == SCALE_MAXIMIZE) {
        scale_data->start_alpha = scale_data->end_alpha = 1.0;
    } else {
        scale_data->start_alpha = view->minimized ? 1 : 0;
        scale_data->end_alpha = view->minimized ? 0 : 1;
    }
}

static bool scale_entity_bouding_box(struct effect_entity *entity, struct kywc_box *box)
{
    struct scale_effect_data *scale_data = entity->user_data;
    *box = scale_data->render_box;

    struct effect_chain *chain = entity->slot.chain;
    struct node_effect_chain *node_chain = wl_container_of(chain, node_chain, base);
    struct wlr_box node_box;
    if (box->width <= 0 || box->height <= 0) {
        node_chain->impl.get_bounding_box(node_chain->node, &node_box);
        box->x = node_box.x;
        box->y = node_box.y;
        box->width = node_box.width;
        box->height = node_box.height;
        return false;
    }

    int lx, ly;
    ky_scene_node_coords(scale_data->node, &lx, &ly);
    box->x -= lx;
    box->y -= ly;
    return false;
}

static bool scale_node_push_damage(struct effect_entity *entity, struct ky_scene_node *damage_node,
                                   uint32_t *damage_type, pixman_region32_t *damage)
{
    struct kywc_box box;
    scale_entity_bouding_box(entity, &box);
    pixman_region32_union_rect(damage, damage, box.x, box.y, box.width, box.height);
    return false;
}

static void handle_view_positon(struct wl_listener *listener, void *data)
{
    struct scale_effect_data *scale_data = wl_container_of(listener, scale_data, view_position);
    scale_calc_start_and_end_geometry(scale_data);
    animator_set_position(scale_data->animator, scale_data->end_geometry.x,
                          scale_data->end_geometry.y);
}

static void handle_view_size(struct wl_listener *listener, void *data)
{
    struct scale_effect_data *scale_data = wl_container_of(listener, scale_data, view_size);
    scale_calc_start_and_end_geometry(scale_data);
    animator_set_size(scale_data->animator, scale_data->end_geometry.width,
                      scale_data->end_geometry.height);
}

static void handle_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct scale_effect_data *scale_data = wl_container_of(listener, scale_data, thumbnail_update);
    struct thumbnail_update_event *event = data;
    if (!event->buffer_changed) {
        return;
    }

    if (scale_data->thumbnail_texture) {
        wlr_texture_destroy(scale_data->thumbnail_texture);
    }
    scale_data->thumbnail_texture =
        wlr_texture_from_buffer(scale->manager->server->renderer, event->buffer);
}

static void handle_thumbnail_destroy(struct wl_listener *listener, void *data)
{
    struct scale_effect_data *scale_data = wl_container_of(listener, scale_data, thumbnail_destroy);
    wl_list_remove(&scale_data->thumbnail_destroy.link);
    wl_list_remove(&scale_data->thumbnail_update.link);
    scale_data->thumbnail = NULL;
}

static bool scale_effect_data_create_thumbnail(struct scale_effect_data *data)
{
    data->thumbnail = thumbnail_create_from_view(data->view, 0, 1.0f);
    if (!data->thumbnail) {
        return false;
    }

    data->thumbnail_update.notify = handle_thumbnail_update;
    thumbnail_add_update_listener(data->thumbnail, &data->thumbnail_update);
    data->thumbnail_destroy.notify = handle_thumbnail_destroy;
    thumbnail_add_destroy_listener(data->thumbnail, &data->thumbnail_destroy);

    return true;
}

static void scale_effect_data_set_animator(struct scale_effect_data *data)
{
    animator_set_position(data->animator, data->end_geometry.x, data->end_geometry.y);
    animator_set_size(data->animator, data->end_geometry.width, data->end_geometry.height);
    animator_set_alpha(data->animator, data->end_alpha);
}

static bool scale_effect_data_init(struct scale_effect_data *scale_data, struct view *view,
                                   enum scale_action action)
{
    scale_data->view = view;
    scale_data->node = &view->tree->node;
    scale_data->action = action;
    scale_init_alpha_and_geometry(scale_data);
    scale_data->start_time = current_time_msec();

    struct animation_data start_data = {
        .alpha = scale_data->start_alpha,
        .angle = 0,
        .geometry = scale_data->start_geometry,
    };
    struct animator *animator = animator_create(&start_data, scale_data->start_time,
                                                scale_data->start_time + scale->duration);
    if (!animator) {
        return false;
    }

    scale_data->animator = animator;
    scale_effect_data_set_animator(scale_data);

    if (!scale_effect_data_create_thumbnail(scale_data)) {
        animator_destroy(scale_data->animator);
        free(scale_data);
        return false;
    }

    scale_data->view_position.notify = handle_view_positon;
    wl_signal_add(&view->base.events.position, &scale_data->view_position);
    scale_data->view_size.notify = handle_view_size;
    wl_signal_add(&view->base.events.size, &scale_data->view_size);

    return true;
}

static bool scale_frame_render_pre(struct effect_entity *entity,
                                   struct ky_scene_output *scene_output)
{
    struct scale_effect_data *data = entity->user_data;
    if (current_time_msec() > data->start_time + scale->duration) {
        effect_entity_destroy(entity);
        return true;
    }

    const struct animation_data *animation_data =
        animator_value(data->animator, current_time_msec());
    data->current = *animation_data;
    ky_scene_node_push_damage(data->node, KY_SCENE_DAMAGE_BOTH, NULL);

    return true;
}

static bool scale_frame_render_post(struct effect_entity *entity,
                                    struct ky_scene_render_target *target)
{
    struct scale_effect_data *data = entity->user_data;
    ky_scene_node_push_damage(data->node, KY_SCENE_DAMAGE_BOTH, NULL);
    return true;
}

static const struct effect_interface scale_effect_impl = {
    .entity_destroy = scale_entity_destroy,
    .entity_bounding_box = scale_entity_bouding_box,
    .node_push_damage = scale_node_push_damage,
    .node_render = scale_node_render,
    .frame_render_pre = scale_frame_render_pre,
    .frame_render_post = scale_frame_render_post,
};

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct effect_entity *entity, *tmp;
    wl_list_for_each_safe(entity, tmp, &scale->effect->entities, effect_link) {
        effect_entity_destroy(entity);
    }

    wl_list_remove(&scale->destroy.link);
    free(scale);
    scale = NULL;
}

bool scale_effect_create(struct effect_manager *manager)
{
    scale = calloc(1, sizeof(*scale));
    if (!scale) {
        return false;
    }

    bool enabled = wlr_renderer_is_opengl(manager->server->renderer);
    scale->effect = effect_create("scale", 2, enabled, &scale_effect_impl);
    if (!scale->effect) {
        free(scale);
        scale = NULL;
        return false;
    }

    scale->manager = manager;
    scale->duration = 300;

    scale->destroy.notify = handle_effect_destroy;
    wl_signal_add(&scale->effect->events.destroy, &scale->destroy);

    return true;
}

static struct scale_effect_data *
scale_effect_data_create(struct effect_entity *entity, struct view *view, enum scale_action action)
{
    struct scale_effect_data *scale_data = calloc(1, sizeof(*scale_data));
    if (!scale_data) {
        effect_entity_destroy(entity);
        return NULL;
    }

    if (!scale_effect_data_init(scale_data, view, action)) {
        free(scale_data);
        effect_entity_destroy(entity);
        return NULL;
    }

    return scale_data;
}

bool view_add_scale_effect(struct view *view, enum scale_action action)
{
    if (!scale || !scale->effect->enabled) {
        return false;
    }

    struct effect_entity *entity = ky_scene_node_add_effect(&view->tree->node, scale->effect);
    if (!entity) {
        return false;
    }

    /* scale effect interrupted */
    if (entity->user_data) {
        if (view->base.minimized) {
            ky_scene_node_set_enabled(&view->tree->node, true);
        }
        struct scale_effect_data *scale_data = entity->user_data;
        scale_data->action = action;
        scale_init_alpha_and_geometry(scale_data);
        scale_effect_data_set_animator(scale_data);
        return true;
    }

    entity->user_data = scale_effect_data_create(entity, view, action);

    if (view->base.minimized) {
        ky_scene_node_set_enabled(&view->tree->node, true);
        ky_scene_node_raise_to_top(view->current_proxy ? &view->current_proxy->tree->node
                                                       : &view->tree->node);
    }

    return true;
}
