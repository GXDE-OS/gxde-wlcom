// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _EFFECT_H_
#define _EFFECT_H_

#include <kywc/boxes.h>

#include "scene/render.h"
#include "scene/scene.h"

struct server;
struct effect;
struct effect_entity;

struct effect_option {
    const char *key;
    union {
        int32_t num;
        const char *str;
        double realnum;
        bool boolean;
    } value;
};

struct effect_interface {
    void (*entity_create)(struct effect_entity *entity);
    void (*entity_destroy)(struct effect_entity *entity);
    void (*entity_enable)(struct effect_entity *entity);

    bool (*entity_bounding_box)(struct effect_entity *entity, struct kywc_box *box);
    bool (*entity_clip_region)(struct effect_entity *entity, pixman_region32_t *clip_region);
    bool (*entity_opaque_region)(struct effect_entity *entity, pixman_region32_t *opaque_region);

    bool (*node_push_damage)(struct effect_entity *entity, struct ky_scene_node *damage_node,
                             uint32_t *damage_type, pixman_region32_t *damage);

    bool (*node_render)(struct effect_entity *entity, int lx, int ly,
                        struct ky_scene_render_target *target);

    bool (*frame_render_pre)(struct effect_entity *entity, struct ky_scene_output *output);
    bool (*frame_render_begin)(struct effect_entity *entity, struct ky_scene_render_target *target);
    void (*frame_render)(struct effect_entity *entity, struct ky_scene_render_target *target);
    bool (*frame_render_end)(struct effect_entity *entity, struct ky_scene_render_target *target);
    bool (*frame_render_post)(struct effect_entity *entity, struct ky_scene_render_target *target);

    /* option is saved if return true */
    bool (*configure)(struct effect *effect, const struct effect_option *option);
};

struct effect {
    struct wl_list link;
    struct wl_list entities; // effect_entity->link

    const char *name;
    int priority;
    bool enabled, destroying;
    uint32_t types;

    const struct effect_interface *impl;
    void *user_data;

    struct {
        struct wl_signal enable;
        struct wl_signal disable;
        struct wl_signal destroy;
    } events;

    struct effect_manager *manager;
    struct json_object *options;
};

struct effect_slot {
    struct wl_list link;
    struct effect_chain *chain;
    struct wl_listener chain_destroy;
};

struct effect_chain {
    struct wl_list slots;
    struct {
        struct wl_signal destroy;
    } events;
};

struct effect_entity {
    struct effect_slot slot;
    struct effect_slot frame_slot;

    struct effect *effect;
    struct wl_list effect_link;
    struct wl_listener effect_enable;
    struct wl_listener effect_disable;
    struct wl_listener effect_destroy;

    void *user_data;
};

struct node_effect_chain {
    struct effect_chain base;

    struct wlr_addon addon;
    struct ky_scene_node *node;
    struct ky_scene_node_interface impl;
};

bool effect_manager_create(struct server *server);

struct effect *effect_create(const char *name, int priority, bool enabled,
                             const struct effect_interface *impl, void *data);

void effect_destroy(struct effect *effect);

void effect_set_enabled(struct effect *effect, bool enabled);

void effect_write_enabled(struct effect *effect, bool enabled);

bool effect_get_option_boolean(struct effect *effect, const char *key, bool value);

void effect_set_option_boolean(struct effect *effect, const char *key, bool value);

int effect_get_option_int(struct effect *effect, const char *key, int value);

void effect_set_option_int(struct effect *effect, const char *key, int value);

double effect_get_option_double(struct effect *effect, const char *key, double value);

void effect_set_option_double(struct effect *effect, const char *key, double value);

const char *effect_get_option_string(struct effect *effect, const char *key, const char *value);

void effect_set_option_string(struct effect *effect, const char *key, const char *value);

void effect_entity_destroy(struct effect_entity *entity);

void effect_entity_push_damage(struct effect_entity *entity, uint32_t damage_type);

struct effect_entity *ky_scene_node_find_effect_entity(struct ky_scene_node *node,
                                                       struct effect *effect);

struct effect_entity *ky_scene_node_add_effect(struct ky_scene_node *node, struct effect *effect);

struct effect_entity *ky_scene_find_effect_entity(struct ky_scene *scene, struct effect *effect);

struct effect_entity *ky_scene_add_effect(struct ky_scene *scene, struct effect *effec);

void ky_scene_output_render_pre(struct ky_scene_output *scene_output);

void ky_scene_output_render_begin(struct ky_scene_render_target *target);

bool ky_scene_output_render(struct ky_scene_render_target *target);

void ky_scene_output_render_end(struct ky_scene_render_target *target);

void ky_scene_output_render_post(struct ky_scene_render_target *target);

#endif /* _EFFECT_H_ */
