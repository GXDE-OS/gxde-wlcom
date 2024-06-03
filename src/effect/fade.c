// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>
#include <string.h>

#include "effect/animator.h"
#include "effect/fade.h"
#include "effect_p.h"
#include "render/opengl.h"
#include "scene/thumbnail.h"
#include "theme.h"
#include "util/time.h"

struct padding {
    int top, bottom, left, right;
};

struct fade_effect_data {
    struct animator *animator;
    struct animation_data current;

    struct kywc_box start_geometry;
    struct kywc_box end_geometry;
    struct kywc_box render_box;
    struct kywc_box view_geometry;
    struct padding shadow;

    float start_alpha, end_alpha;
    int64_t start_time;

    enum fade_action action;
    int duration;

    struct view *view;
    struct wl_listener view_size;
    struct wl_listener view_position;
    struct wl_listener view_destroy;

    struct ky_scene_buffer *buffer;
    struct wl_listener buffer_destroy;

    struct thumbnail *thumbnail;
    struct wlr_texture *thumbnail_texture;
    struct wlr_buffer *thumbnail_buffer;
    struct wl_listener thumbnail_update;
    struct wl_listener thumbnail_destroy;
};

struct fade_effect {
    struct effect *effect;
    struct effect_manager *manager;

    struct wl_listener destroy;
};

static struct fade_effect *fade = NULL;

static void calc_view_box(struct kywc_view *view, struct kywc_box *geometry_box,
                          struct kywc_box *box)
{
    box->x = geometry_box->x - view->margin.off_x;
    box->y = geometry_box->y - view->margin.off_y;
    box->width = geometry_box->width + view->margin.off_width;
    box->height = geometry_box->height + view->margin.off_height;
}

static void fade_calc_shadow(struct kywc_view *view, struct padding *shadow)
{
    struct theme *theme = theme_manager_get_current();
    if (view->ssd == KYWC_SSD_NONE) {
        memcpy(shadow, &view->padding, sizeof(struct padding));
    } else {
        shadow->top = shadow->bottom = theme->shadow_border;
        shadow->left = shadow->right = theme->shadow_border;
    }
}

static void fade_calc_start_and_end_geometry(struct fade_effect_data *data)
{
    struct kywc_view *kywc_view = &data->view->base;
    struct kywc_box end_box = { 0 };
    if (data->action == FADE_MAP) {
        /* Size changed from 90% to 100% of the current window when map */
        end_box.width = kywc_view->geometry.width * 0.9;
        end_box.height = kywc_view->geometry.height * 0.9;
        end_box.x = kywc_view->geometry.x + end_box.width * 0.05;
        end_box.y = kywc_view->geometry.y + end_box.height * 0.05;
    } else {
        /* Size changed from 100% to 80% of the current window when unmap */
        end_box.width = kywc_view->geometry.width * 0.8;
        end_box.height = kywc_view->geometry.height * 0.8;
        end_box.x = kywc_view->geometry.x + end_box.width * 0.1;
        end_box.y = kywc_view->geometry.y + end_box.height * 0.1;
    }

    if ((current_time_msec() < data->start_time + data->duration)) {
        struct kywc_box start_geometry = data->action ? kywc_view->geometry : end_box;
        calc_view_box(kywc_view, &start_geometry, &data->start_geometry);
    }
    struct kywc_box end_geometry = data->action ? end_box : kywc_view->geometry;
    calc_view_box(kywc_view, &end_geometry, &data->end_geometry);
}

static void fade_data_init_alpha_and_geometry(struct fade_effect_data *fade_data)
{
    fade_calc_start_and_end_geometry(fade_data);
    if (fade_data->action == FADE_MAP) {
        fade_data->start_alpha = 0;
        fade_data->end_alpha = 1.0;
    } else {
        fade_data->start_alpha = 1.0;
        fade_data->end_alpha = 0;
    }
}

static void fade_entity_push_damage(struct effect_entity *entity)
{
    struct effect_chain *chain = entity->slot.chain;
    struct node_effect_chain *node_chain = wl_container_of(chain, node_chain, base);
    ky_scene_node_push_damage(node_chain->node, KY_SCENE_DAMAGE_BOTH, NULL);
}

static bool fade_frame_render_pre(struct effect_entity *entity,
                                  struct ky_scene_output *scene_output)
{
    struct fade_effect_data *fade_data = entity->user_data;
    if (current_time_msec() > fade_data->start_time + fade_data->duration) {
        effect_entity_destroy(entity);
        return true;
    }

    const struct animation_data *animation_data =
        animator_value(fade_data->animator, current_time_msec());
    fade_data->current = *animation_data;
    fade_entity_push_damage(entity);

    return true;
}

static bool fade_entity_bouding_box(struct effect_entity *entity, struct kywc_box *box)
{
    struct fade_effect_data *fade_data = entity->user_data;
    *box = fade_data->render_box;

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
    ky_scene_node_coords(node_chain->node, &lx, &ly);
    box->x -= lx;
    box->y -= ly;

    return false;
}

static bool fade_node_push_damage(struct effect_entity *entity, struct ky_scene_node *damage_node,
                                  uint32_t *damage_type, pixman_region32_t *damage)
{
    struct kywc_box box;
    fade_entity_bouding_box(entity, &box);
    pixman_region32_union_rect(damage, damage, box.x, box.y, box.width, box.height);
    return false;
}

static void fade_effect_data_set_animator(struct fade_effect_data *data)
{
    animator_set_position(data->animator, data->end_geometry.x, data->end_geometry.y);
    animator_set_size(data->animator, data->end_geometry.width, data->end_geometry.height);
    animator_set_alpha(data->animator, data->end_alpha);
}

static void handle_view_positon(struct wl_listener *listener, void *data)
{
    struct fade_effect_data *fade_data = wl_container_of(listener, fade_data, view_position);
    fade_calc_start_and_end_geometry(fade_data);

    if (fade_data->animator) {
        animator_destroy(fade_data->animator);
    }
    struct animation_data start = {
        .alpha = fade_data->start_alpha,
        .angle = 0,
        .geometry = fade_data->start_geometry,
    };
    struct animation_type_group type = {
        .geometry = ANIMATION_TYPE_EASE,
        .alpha = ANIMATION_TYPE_EASE,
    };
    fade_data->animator = animator_create(&start, type, fade_data->start_time,
                                          fade_data->start_time + fade_data->duration);

    fade_effect_data_set_animator(fade_data);
}

static void handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct fade_effect_data *fade_data = wl_container_of(listener, fade_data, view_destroy);

    wl_list_remove(&fade_data->view_destroy.link);
    wl_list_remove(&fade_data->view_position.link);
    wl_list_remove(&fade_data->view_size.link);

    wl_list_init(&fade_data->view_destroy.link);
    wl_list_init(&fade_data->view_position.link);
    wl_list_init(&fade_data->view_destroy.link);
}

static void handle_view_size(struct wl_listener *listener, void *data)
{
    struct fade_effect_data *fade_data = wl_container_of(listener, fade_data, view_size);
    fade_calc_start_and_end_geometry(fade_data);

    if (fade_data->animator) {
        animator_destroy(fade_data->animator);
    }
    struct animation_data start = {
        .alpha = fade_data->start_alpha,
        .angle = 0,
        .geometry = fade_data->start_geometry,
    };
    struct animation_type_group type = {
        .geometry = ANIMATION_TYPE_EASE,
        .alpha = ANIMATION_TYPE_EASE,
    };
    fade_data->animator = animator_create(&start, type, fade_data->start_time,
                                          fade_data->start_time + fade_data->duration);

    fade_effect_data_set_animator(fade_data);
}

static void handle_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct fade_effect_data *fade_data = wl_container_of(listener, fade_data, thumbnail_update);

    struct thumbnail_update_event *event = data;
    if (!event->buffer_changed) {
        return;
    }

    if (fade_data->thumbnail_texture) {
        wlr_texture_destroy(fade_data->thumbnail_texture);
    }
    fade_data->thumbnail_buffer = event->buffer;
    fade_data->thumbnail_texture =
        wlr_texture_from_buffer(fade->manager->server->renderer, event->buffer);
}

static void handle_thumbnail_destroy(struct wl_listener *listener, void *data)
{
    struct fade_effect_data *fade_data = wl_container_of(listener, fade_data, thumbnail_destroy);
    wl_list_remove(&fade_data->thumbnail_destroy.link);
    wl_list_remove(&fade_data->thumbnail_update.link);

    fade_data->thumbnail = NULL;
}

static bool fade_effect_data_create_thumbnail(struct fade_effect_data *data)
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

static bool fade_frame_render_post(struct effect_entity *entity,
                                   struct ky_scene_render_target *target)
{
    fade_entity_push_damage(entity);
    return true;
}

static void fade_calc_render_box(struct fade_effect_data *data, struct padding *padding)
{
    /* shadow */
    struct kywc_box *geometry = &data->current.geometry;
    float width_scale = 0, height_scale = 0;
    width_scale = 1.0 * geometry->width / (data->view_geometry.width);
    height_scale = 1.0 * geometry->height / (data->view_geometry.height);

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

static void fade_data_destroy(struct fade_effect_data *data)
{
    if (data->buffer) {
        wl_list_remove(&data->buffer_destroy.link);
        ky_scene_node_destroy(&data->buffer->node);
    }
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

    if (data->action == FADE_MAP) {
        wl_list_remove(&data->view_position.link);
        wl_list_remove(&data->view_size.link);
        wl_list_remove(&data->view_destroy.link);
    }

    free(data);
}

static void fade_entity_destroy(struct effect_entity *entity)
{
    struct fade_effect_data *data = entity->user_data;
    if (!data) {
        return;
    }

    fade_data_destroy(data);
}

static bool fade_node_render(struct effect_entity *entity, int lx, int ly,
                             struct ky_scene_render_target *target)
{
    struct fade_effect_data *data = entity->user_data;
    if (!target->output || !data->thumbnail_texture) {
        return false;
    }

    fade_calc_render_box(data, &data->shadow);
    data->current.geometry = data->render_box;
    animator_render_texture(&data->current, target, data->thumbnail_texture);

    return false;
}

static void fade_effect_data_reinit(struct fade_effect_data *fade_data, struct animation_data *data)
{
    if (!data) {
        return;
    }
    fade_data->start_geometry = data->geometry;
    fade_data->start_alpha = data->alpha;
    fade_data->end_geometry.width = data->geometry.width * 0.8;
    fade_data->end_geometry.height = data->geometry.height * 0.8;
    fade_data->end_geometry.x = data->geometry.x + data->geometry.width * 0.1;
    fade_data->end_geometry.y = data->geometry.y + data->geometry.height * 0.1;
}

static bool fade_effect_data_init(struct fade_effect_data *fade_data, struct view *view,
                                  enum fade_action action, struct animation_data *animation_data)
{
    fade_data->view = view;
    fade_data->action = action;
    fade_data->duration = action ? 260 : 300;
    fade_data->buffer = NULL;
    fade_data->start_time = current_time_msec();
    struct kywc_view *kywc_view = &view->base;
    fade_data->view_geometry.width = kywc_view->geometry.width + kywc_view->margin.off_width;
    fade_data->view_geometry.height = kywc_view->geometry.height + kywc_view->margin.off_height;
    fade_calc_shadow(&view->base, &fade_data->shadow);
    fade_data_init_alpha_and_geometry(fade_data);

    if (!fade_effect_data_create_thumbnail(fade_data)) {
        animator_destroy(fade_data->animator);
        free(fade_data);
        return false;
    }
    if (action == FADE_UNMAP) {
        thumbnail_update(fade_data->thumbnail);
    }

    fade_effect_data_reinit(fade_data, animation_data);
    struct animation_data start = {
        .alpha = fade_data->start_alpha,
        .angle = 0,
        .geometry = fade_data->start_geometry,
    };

    struct animation_type_group type = {
        .geometry = ANIMATION_TYPE_EASE,
        .alpha = ANIMATION_TYPE_EASE,
    };
    struct animator *animator = animator_create(&start, type, fade_data->start_time,
                                                fade_data->start_time + fade_data->duration);
    if (!animator) {
        return false;
    }

    fade_data->animator = animator;
    fade_effect_data_set_animator(fade_data);

    if (action == FADE_MAP) {
        fade_data->view_position.notify = handle_view_positon;
        wl_signal_add(&view->base.events.position, &fade_data->view_position);
        fade_data->view_size.notify = handle_view_size;
        wl_signal_add(&view->base.events.size, &fade_data->view_size);
        fade_data->view_destroy.notify = handle_view_destroy;
        wl_signal_add(&view->base.events.destroy, &fade_data->view_destroy);
    }

    return true;
}

static const struct effect_interface fade_effect_impl = {
    .entity_destroy = fade_entity_destroy,
    .entity_bounding_box = fade_entity_bouding_box,
    .node_push_damage = fade_node_push_damage,
    .node_render = fade_node_render,
    .frame_render_pre = fade_frame_render_pre,
    .frame_render_post = fade_frame_render_post,
};

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct effect_entity *entity, *tmp;
    wl_list_for_each_safe(entity, tmp, &fade->effect->entities, effect_link) {
        effect_entity_destroy(entity);
    }

    wl_list_remove(&fade->destroy.link);
    free(fade);
    fade = NULL;
}

bool fade_effect_create(struct effect_manager *manager)
{
    fade = calloc(1, sizeof(*fade));
    if (!fade) {
        return false;
    }

    bool enabled = wlr_renderer_is_opengl(manager->server->renderer);
    fade->effect = effect_create("fade", 3, enabled, &fade_effect_impl);
    if (!fade->effect) {
        free(fade);
        fade = NULL;
        return false;
    }

    fade->manager = manager;

    fade->destroy.notify = handle_effect_destroy;
    wl_signal_add(&fade->effect->events.destroy, &fade->destroy);

    return true;
}

static void handle_buffer_destroy(struct wl_listener *listener, void *data)
{
    struct fade_effect_data *fade_data = wl_container_of(listener, fade_data, buffer_destroy);
    wl_list_remove(&fade_data->buffer_destroy.link);
    fade_data->buffer = NULL;
}

static bool fade_create_scene_buffer(struct view *view, struct fade_effect_data *data)
{
    if (!data->thumbnail_buffer) {
        return false;
    }

    bool in_workspace = view->current_proxy ? true : false;
    struct ky_scene_tree *view_parent = view->tree->node.parent;
    if (in_workspace) {
        view_parent = view_parent->node.parent;
    }

    struct ky_scene_buffer *buffer = ky_scene_buffer_create(view_parent, data->thumbnail_buffer);
    if (!buffer) {
        return false;
    }
    struct effect_entity *entity = ky_scene_node_add_effect(&buffer->node, fade->effect);
    if (!entity) {
        ky_scene_node_destroy(&buffer->node);
        return false;
    }

    data->buffer_destroy.notify = handle_buffer_destroy;
    wl_signal_add(&buffer->node.events.destroy, &data->buffer_destroy);

    ky_scene_node_raise_to_top(&buffer->node);
    data->buffer = buffer;
    entity->user_data = data;

    return true;
}

static bool unmap_add_fade_effect(struct view *view, enum fade_action action,
                                  struct animation_data *animation_data)
{
    struct fade_effect_data *data = calloc(1, sizeof(*data));
    if (!data) {
        return false;
    }

    if (!fade_effect_data_init(data, view, action, animation_data)) {
        free(data);
        return false;
    }

    if (!fade_create_scene_buffer(view, data)) {
        fade_data_destroy(data);
        return false;
    }

    return true;
}

static struct fade_effect_data *fade_effect_data_create(struct effect_entity *entity,
                                                        struct view *view, enum fade_action action)
{
    struct fade_effect_data *fade_data = calloc(1, sizeof(*fade_data));
    if (!fade_data) {
        effect_entity_destroy(entity);
        return NULL;
    }

    if (!fade_effect_data_init(fade_data, view, action, NULL)) {
        free(fade_data);
        effect_entity_destroy(entity);
        return NULL;
    }

    return fade_data;
}

bool view_add_fade_effect(struct view *view, enum fade_action action)
{
    if (!fade || !fade->effect->enabled) {
        return false;
    }

    struct effect_entity *entity = ky_scene_node_add_effect(&view->tree->node, fade->effect);
    if (!entity) {
        return false;
    }

    /* fade effect interrupted */
    if (entity->user_data != NULL && (action == FADE_UNMAP)) {
        struct fade_effect_data *data = entity->user_data;
        if (!unmap_add_fade_effect(view, action, &data->current)) {
            return false;
        }
        effect_entity_destroy(entity);
        return true;
    }

    if (action == FADE_UNMAP) {
        effect_entity_destroy(entity);
        return unmap_add_fade_effect(view, action, NULL);
    }

    entity->user_data = fade_effect_data_create(entity, view, action);

    return true;
}
