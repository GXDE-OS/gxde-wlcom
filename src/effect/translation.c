// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdbool.h>
#include <stdlib.h>

#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_output.h>

#include "effect/animator.h"
#include "effect/translation.h"
#include "effect_p.h"
#include "render/pass.h"
#include "render/renderer.h"
#include "scene/render.h"
#include "scene/thumbnail.h"
#include "util/time.h"
#include "view/view.h"

struct translation_entity {
    struct wl_list link;
    struct workspace *workspace;

    int index;
    struct kywc_box end_geometry;
    struct kywc_box start_geometry;

    struct animator *animator;
    struct animation_data current;

    struct wlr_texture *thumbnail_texture;
    struct wl_listener thumbnail_update;
};

struct workspace_output {
    struct wl_list link;
    struct ky_scene_output *scene_output;

    int width, height;

    struct wl_list entities;
};

struct workspace_translation {
    uint32_t start_time, duration, end_time;

    struct wl_list workspace_outputs;
};

struct translation_effect {
    struct effect *effect;

    int duration;
    struct ky_scene *scene;
    struct wlr_renderer *renderer;

    struct wl_listener destroy;
};

static struct translation_effect *translation_effect = NULL;

static struct workspace_output *find_workspace_output(struct workspace_translation *data,
                                                      struct ky_scene_output *scene_output);

static void workspace_translation_destroy(struct workspace_translation *data);

static void translation_effect_entity_destroy(struct effect_entity *entity)
{
    struct workspace_translation *data = entity->user_data;

    workspace_translation_destroy(data);
    entity->user_data = NULL;
}

static bool translation_frame_render_pre(struct effect_entity *entity,
                                         struct ky_scene_render_target *target)
{
    struct workspace_translation *data = entity->user_data;
    uint32_t current_time = current_time_msec();
    if (data->end_time < current_time) {
        effect_entity_destroy(entity);
        return false;
    }

    struct workspace_output *ws_output = find_workspace_output(data, target->output);
    if (!ws_output) {
        return false;
    }

    const struct animation_data *entity_data;
    struct translation_entity *translation_entity;
    wl_list_for_each(translation_entity, &ws_output->entities, link) {
        entity_data = animator_value(translation_entity->animator, current_time);
        translation_entity->current = *entity_data;
    }

    return false;
}

static void translation_entity_render(struct translation_entity *translation_entity,
                                      struct ky_scene_render_target *target)
{
    if (!translation_entity->thumbnail_texture) {
        return;
    }

    struct wlr_box dst_box = {
        .x = translation_entity->current.geometry.x,
        .y = translation_entity->current.geometry.y,
        .width = translation_entity->current.geometry.width,
        .height = translation_entity->current.geometry.height,
    };

    pixman_region32_t render_region;
    pixman_region32_init(&render_region);
    pixman_region32_copy(&render_region, &target->damage);
    pixman_region32_translate(&render_region, -target->logical.x, -target->logical.y);

    pixman_region32_intersect_rect(&render_region, &render_region, dst_box.x, dst_box.y,
                                   dst_box.width, dst_box.height);
    if (!pixman_region32_not_empty(&render_region)) {
        pixman_region32_fini(&render_region);
        return;
    }

    ky_scene_render_box(&dst_box, target);
    ky_scene_render_region(&render_region, target);

    struct ky_render_texture_options options = {
        .base = {
            .texture = translation_entity->thumbnail_texture,
            .alpha = &translation_entity->current.alpha,
            .dst_box = dst_box,
            .clip = &render_region,
        },
        .radius = { 0 },
        .repeated = false,
        .rotation_angle = translation_entity->current.angle,
    };
    ky_render_pass_add_texture(target->render_pass, &options);

    pixman_region32_fini(&render_region);
}

static void translation_frame_render(struct effect_entity *entity,
                                     struct ky_scene_render_target *target)
{
    struct workspace_translation *data = entity->user_data;
    struct workspace_output *ws_output = find_workspace_output(data, target->output);
    if (!ws_output) {
        return;
    }

    /* clear the target buffer */
    wlr_render_pass_add_rect(target->render_pass, &(struct wlr_render_rect_options){
                                                      .color = { 0, 0, 0, 0 },
                                                      .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
                                                  });

    struct translation_entity *translation_entity;
    wl_list_for_each_reverse(translation_entity, &ws_output->entities, link) {
        translation_entity_render(translation_entity, target);
    }
    return;
}

static bool translation_frame_render_post(struct effect_entity *entity,
                                          struct ky_scene_render_target *target)
{
    ky_scene_output_damage_whole(target->output);
    return false;
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&translation_effect->destroy.link);
    free(translation_effect);
    translation_effect = NULL;
}

static bool handle_translation_effect_configure(struct effect *effect,
                                                const struct effect_option *option)
{
    if (effect_option_is_enabled_option(option)) {
        return true;
    }

    return false;
}

static const struct effect_interface effect_impl = {
    .entity_destroy = translation_effect_entity_destroy,
    .frame_render_pre = translation_frame_render_pre,
    .frame_render = translation_frame_render,
    .frame_render_post = translation_frame_render_post,
    .configure = handle_translation_effect_configure,
};

bool translation_effect_create(struct effect_manager *manager)
{
    translation_effect = calloc(1, sizeof(*translation_effect));
    if (!translation_effect) {
        return false;
    }

    translation_effect->duration = 400;
    translation_effect->scene = manager->server->scene;
    translation_effect->renderer = manager->server->renderer;

    bool enable = !ky_renderer_is_software(manager->server->renderer);
    translation_effect->effect = effect_create("translation", 105, enable, &effect_impl, NULL);
    if (!translation_effect->effect) {
        free(translation_effect);
        translation_effect = NULL;
        return false;
    }

    translation_effect->destroy.notify = handle_effect_destroy;
    wl_signal_add(&translation_effect->effect->events.destroy, &translation_effect->destroy);
    return true;
}

static void handle_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct translation_entity *translation_entity =
        wl_container_of(listener, translation_entity, thumbnail_update);

    struct thumbnail_update_event *event = data;
    if (!event->buffer_changed) {
        return;
    }

    if (translation_entity->thumbnail_texture) {
        wlr_texture_destroy(translation_entity->thumbnail_texture);
    }
    translation_entity->thumbnail_texture =
        wlr_texture_from_buffer(translation_effect->renderer, event->buffer);
}

static void translation_entity_calc_start_pos(struct translation_entity *translation_entity,
                                              struct translation_entity *pre_entity,
                                              int entity_width, int entity_height,
                                              enum direction direct)
{
    struct kywc_box *current = &pre_entity->current.geometry;
    translation_entity->start_geometry.x = 0;
    translation_entity->start_geometry.y = 0;
    switch (direct) {
    case DIRECTION_LEFT:
        translation_entity->start_geometry.x = current->x - entity_width;
        break;
    case DIRECTION_RIGHT:
        translation_entity->start_geometry.x = current->x + entity_width;
        break;
    case DIRECTION_UP:
        translation_entity->start_geometry.y = current->y - entity_height;
        break;
    case DIRECTION_DOWN:
        translation_entity->start_geometry.y = current->y + entity_height;
        break;
    }
    translation_entity->current.geometry = translation_entity->start_geometry;
}

static void translation_entity_destroy(struct translation_entity *translation_entity)
{
    if (translation_entity->thumbnail_texture) {
        wlr_texture_destroy(translation_entity->thumbnail_texture);
    }
    if (translation_entity->animator) {
        animator_destroy(translation_entity->animator);
    }

    wl_list_remove(&translation_entity->link);
    free(translation_entity);
}

static struct translation_entity *translation_entity_create(struct workspace_output *ws_output,
                                                            struct workspace *workspace,
                                                            enum direction direct)
{
    struct translation_entity *translation_entity = calloc(1, sizeof(*translation_entity));
    if (!translation_entity) {
        return NULL;
    }

    translation_entity->index = -1;
    translation_entity->workspace = workspace;
    translation_entity->end_geometry.x = translation_entity->end_geometry.y = 0;
    translation_entity->end_geometry.width = ws_output->width;
    translation_entity->end_geometry.height = ws_output->height;
    translation_entity->start_geometry = translation_entity->end_geometry;

    translation_entity->current.alpha = 1;
    translation_entity->current.angle = 0;
    translation_entity->current.geometry = translation_entity->start_geometry;
    translation_entity->animator = NULL;

    wl_list_init(&translation_entity->link);

    struct thumbnail *output_thumbnail =
        thumbnail_create_from_output(ws_output->scene_output, NULL, 1.0);
    if (!output_thumbnail) {
        return NULL;
    }

    translation_entity->thumbnail_texture = NULL;
    translation_entity->thumbnail_update.notify = handle_thumbnail_update;
    thumbnail_add_update_listener(output_thumbnail, &translation_entity->thumbnail_update);
    thumbnail_update(output_thumbnail);

    wl_list_remove(&translation_entity->thumbnail_update.link);
    thumbnail_destroy(output_thumbnail);

    return translation_entity;
}

static struct translation_entity *find_translation_entity(struct workspace_output *ws_output,
                                                          struct workspace *workspace)
{
    struct translation_entity *exist_entity;
    wl_list_for_each_reverse(exist_entity, &ws_output->entities, link) {
        if (exist_entity->workspace == workspace) {
            return exist_entity;
        }
    }

    return NULL;
}

static void workspace_output_reset_entities_animator(struct workspace_output *ws_output,
                                                     int end_time)
{
    int64_t current_time = current_time_msec();
    struct translation_entity *exist_entity;
    wl_list_for_each(exist_entity, &ws_output->entities, link) {
        if (exist_entity->animator) {
            animator_set_time_ex(exist_entity->animator, current_time, end_time);
            animator_set_position(exist_entity->animator, exist_entity->end_geometry.x,
                                  exist_entity->end_geometry.y);
            continue;
        }

        struct animation_type_group type_group = {
            .geometry = ANIMATION_TYPE_EASE_OUT,
        };
        exist_entity->animator =
            animator_create(&exist_entity->current, type_group, current_time, end_time);
        if (!exist_entity->animator) {
            continue;
        }

        animator_set_position(exist_entity->animator, exist_entity->end_geometry.x,
                              exist_entity->end_geometry.y);
    }
}

static void workspace_output_add_translation_entity(struct workspace_output *ws_output,
                                                    struct translation_entity *translation_entity,
                                                    enum direction direct, int end_time)
{
    struct translation_entity *exist_entity, *head_entity = NULL;
    /* translation_entity don't in list */
    if (translation_entity->index == -1) {
        wl_list_for_each_reverse(exist_entity, &ws_output->entities, link) {
            if (direct == DIRECTION_LEFT) {
                exist_entity->end_geometry.x += ws_output->width;
            } else if (direct == DIRECTION_RIGHT) {
                exist_entity->end_geometry.x -= ws_output->width;
            } else if (direct == DIRECTION_UP) {
                exist_entity->end_geometry.y += ws_output->height;
            } else if (direct == DIRECTION_DOWN) {
                exist_entity->end_geometry.y -= ws_output->height;
            }
            exist_entity->index += 1;
            head_entity = exist_entity;
        }
        if (head_entity) {
            translation_entity_calc_start_pos(translation_entity, head_entity, ws_output->width,
                                              ws_output->height, direct);
        }

        translation_entity->index = 0;
        translation_entity->end_geometry.x = translation_entity->end_geometry.y = 0;
        wl_list_insert(&ws_output->entities, &translation_entity->link);
        workspace_output_reset_entities_animator(ws_output, end_time);
        return;
    }

    int index_offset = 1;
    int offset_x = translation_entity->end_geometry.x;
    int offset_y = translation_entity->end_geometry.y;
    wl_list_for_each(exist_entity, &ws_output->entities, link) {
        /* index don't change that entity after inserted entity in list */
        if (exist_entity == translation_entity) {
            index_offset = 0;
        }
        exist_entity->index += index_offset;
        exist_entity->end_geometry.x -= offset_x;
        exist_entity->end_geometry.y -= offset_y;
    }

    wl_list_remove(&translation_entity->link);

    translation_entity->index = 0;
    wl_list_insert(&ws_output->entities, &translation_entity->link);
    workspace_output_reset_entities_animator(ws_output, end_time);
}

static void workspace_output_destroy(struct workspace_output *ws_output)
{
    struct translation_entity *exist_entity, *tmp;
    wl_list_for_each_safe(exist_entity, tmp, &ws_output->entities, link) {
        translation_entity_destroy(exist_entity);
    }

    wl_list_remove(&ws_output->link);
    free(ws_output);
}

static struct workspace_output *workspace_output_create(struct ky_scene_output *output)
{
    struct workspace_output *workspace_output = calloc(1, sizeof(*workspace_output));
    if (!workspace_output) {
        return NULL;
    }

    workspace_output->scene_output = output;
    wlr_output_effective_resolution(output->output, &workspace_output->width,
                                    &workspace_output->height);

    wl_list_init(&workspace_output->entities);
    wl_list_init(&workspace_output->link);
    return workspace_output;
}

static struct workspace_output *find_workspace_output(struct workspace_translation *data,
                                                      struct ky_scene_output *output)
{
    struct workspace_output *ws_output;
    wl_list_for_each(ws_output, &data->workspace_outputs, link) {
        if (ws_output->scene_output == output) {
            return ws_output;
        }
    }
    return NULL;
}

static bool workspace_translation_add_workspace(struct workspace_translation *data,
                                                struct ky_scene_output *scene_output,
                                                struct workspace *workspace, enum direction direct)
{
    struct workspace_output *ws_output;
    ws_output = find_workspace_output(data, scene_output);
    if (ws_output) {
        goto final;
    }

    ws_output = workspace_output_create(scene_output);
    if (!ws_output) {
        return false;
    }

    wl_list_insert(&data->workspace_outputs, &ws_output->link);

    struct translation_entity *translation_entity;
final:
    translation_entity = find_translation_entity(ws_output, workspace);
    if (translation_entity && translation_entity->index == 0) {
        return true;
    } else if (translation_entity) {
        workspace_output_add_translation_entity(ws_output, translation_entity, direct,
                                                data->end_time);
        return true;
    }

    translation_entity = translation_entity_create(ws_output, workspace, direct);
    if (!translation_entity) {
        return false;
    }

    workspace_output_add_translation_entity(ws_output, translation_entity, direct, data->end_time);
    return true;
}

static void workspace_translation_destroy(struct workspace_translation *data)
{
    struct workspace_output *ws_output, *tmp;
    wl_list_for_each_safe(ws_output, tmp, &data->workspace_outputs, link) {
        workspace_output_destroy(ws_output);
    }
    free(data);
}

static struct workspace_translation *workdspace_translation_create(void)
{
    struct workspace_translation *data = calloc(1, sizeof(*data));
    if (!data) {
        return NULL;
    }

    data->duration = translation_effect->duration;
    data->start_time = current_time_msec();
    data->end_time = data->start_time + data->duration;

    wl_list_init(&data->workspace_outputs);
    return data;
}

bool workspace_add_translation_effect(struct workspace *current, struct workspace *next,
                                      enum direction direct)
{
    if (!translation_effect || !translation_effect->effect->enabled || next == current) {
        return false;
    }

    if (direct < DIRECTION_LEFT || direct > DIRECTION_DOWN) {
        direct = current->position < next->position ? DIRECTION_RIGHT : DIRECTION_LEFT;
    }

    struct workspace_translation *data;
    struct effect_entity *entity =
        ky_scene_find_effect_entity(translation_effect->scene, translation_effect->effect);
    if (entity) {
        data = entity->user_data;
        goto final;
    }

    /*
     * When switching workspaces, the damage region only contains
     * the damage regions generated by the view in the workspace layer.
     */
    ky_scene_damage_whole(translation_effect->scene);

    entity = ky_scene_add_effect(translation_effect->scene, translation_effect->effect);
    if (!entity) {
        return false;
    }

    data = workdspace_translation_create();
    if (!data) {
        effect_entity_destroy(entity);
        return false;
    }

    entity->user_data = data;

final:
    data->end_time = current_time_msec() + data->duration;

    struct ky_scene_output *output;
    wl_list_for_each(output, &translation_effect->scene->outputs, link) {
        if (!workspace_translation_add_workspace(data, output, current, direct)) {
            effect_entity_destroy(entity);
            workspace_translation_destroy(data);
            return false;
        }
    }

    workspace_activate(next);

    wl_list_for_each(output, &translation_effect->scene->outputs, link) {
        if (!workspace_translation_add_workspace(data, output, next, direct)) {
            effect_entity_destroy(entity);
            workspace_translation_destroy(data);
            return false;
        }
    }

    return true;
}
