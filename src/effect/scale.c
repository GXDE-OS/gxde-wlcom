// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#include <kywc/log.h>

#include "effect/effect.h"
#include "effect/scale.h"
#include "effect_p.h"
#include "scene/scene.h"
#include "view/view.h"

static const int priority = 1;

static const char *effect_name = "scale";

enum kywc_effects_state {
    EFFECTS_END = 0,
    EFFECTS_ZOOMING = 1 << 0,
};

struct effect_scale_data {
    struct view *view;

    struct kywc_box last_box;
    struct kywc_box dst_box;
};

struct scale_effect {
    struct effect *effect;
    struct wl_listener destroy;

    struct ky_scene *scene;
    struct effect_manager *manager;
};

static struct scale_effect *scale;

static int test_count = 0;

static void calc_view_box(struct kywc_view *view, struct kywc_box *geometry_box,
                          struct kywc_box *render_box)
{
    render_box->x = geometry_box->x - view->margin.off_x;
    render_box->y = geometry_box->y - view->margin.off_y;
    render_box->width = geometry_box->width + view->margin.off_width;
    render_box->height = geometry_box->height + view->margin.off_height;
}

static void maximize_view_box_init(struct effect_scale_data *data)
{
    struct kywc_view *view = &data->view->base;

    if (view->maximized) {
        calc_view_box(view, &data->view->saved.geometry, &data->last_box);
    } else {
        data->last_box = data->dst_box;
    }
    calc_view_box(view, &view->geometry, &data->dst_box);
}

static void effect_scale_create_params(struct effect_entity *entity)
{
    struct effect_scale_data *scale_data = malloc(sizeof(*scale_data));
    if (!scale_data) {
        return;
    }
    entity->usr_data = scale_data;
}

static void effect_scale_destroy_params(struct effect_entity *entity)
{
    struct effect_scale_data *scale_data = entity->usr_data;
    free(scale_data);
}

static bool effect_scale_render_post(struct effect_entity *entity,
                                     struct ky_scene_render_target *target);

static bool effect_scale_render(struct effect_entity *entity, int lx, int ly,
                                struct ky_scene_render_target *target)
{
    kywc_log(KYWC_INFO, "scale view box rener");

    return false;
}

static bool effect_scale_render_pre(struct effect_entity *entity,
                                    struct ky_scene_output *scene_output)
{
    if (test_count < 0) {
        effect_entity_destroy(entity);
    }
    test_count--;
    effect_scale_render_post(entity, NULL);
    return true;
}

static bool effect_scale_render_post(struct effect_entity *entity,
                                     struct ky_scene_render_target *target)
{
    pixman_region32_t damage_region;
    pixman_region32_init_rect(&damage_region, 0, 0, 1920, 1080);
    ky_scene_add_damage(scale->scene, &damage_region);
    pixman_region32_fini(&damage_region);
    return true;
}

static const struct effect_interface scale_effect_impl = {
    .entity_create = effect_scale_create_params,
    .entity_destroy = effect_scale_destroy_params,
    .node_render = effect_scale_render,
    .frame_render_pre = effect_scale_render_pre,
    .frame_render_post = effect_scale_render_post,
};

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct scale_effect *effect = wl_container_of(listener, effect, destroy);
    struct effect_entity *entity, *tmp;
    wl_list_for_each_safe(entity, tmp, &effect->effect->entities, effect_link) {
        effect_entity_destroy(entity);
    }

    free(effect);
}

bool scale_effect_create(struct effect_manager *manager)
{
    struct scale_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }

    effect->effect = effect_create(effect_name, priority, true, &scale_effect_impl);
    if (!effect->effect) {
        free(effect);
        return false;
    }

    effect->manager = manager;
    effect->scene = manager->server->scene;

    effect->destroy.notify = handle_effect_destroy;
    wl_signal_add(&effect->effect->events.destroy, &effect->destroy);

    scale = effect;
    return true;
}

bool view_add_maximize_effect(struct view *view)
{
    if (!scale || !scale->effect->enabled) {
        return false;
    }
    struct ky_scene_tree *tree = view->tree;
    struct effect_entity *entity = ky_scene_node_add_effect(&tree->node, scale->effect);
    if (!entity) {
        return false;
    }

    struct effect_scale_data *scale_data = entity->usr_data;
    scale_data->view = view;

    maximize_view_box_init(scale_data);
    test_count = 0;
    return true;
}
