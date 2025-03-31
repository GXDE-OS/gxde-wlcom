// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _EFFECT_SHAPE_H_
#define _EFFECT_SHAPE_H_

#include <stdbool.h>
#include <stdint.h>

struct effect_manager;
struct tap_ripple_effect;
struct circle_progressbar_effect;
struct trail_effect;

struct tap_ripple_effect_options {
    float color[3];
    int32_t size;
    uint32_t animate_duration;
    float start_radius;
    float end_radius;
    float start_attenuation;
    float end_attenuation;
};

struct tap_ripple_effect_interface {
    void (*enable)(struct tap_ripple_effect *base_effect, void *user_data);
    void (*disable)(struct tap_ripple_effect *base_effect, void *user_data);
    void (*destroy)(struct tap_ripple_effect *base_effect, void *user_data);
};

struct tap_ripple_effect *tap_ripple_effect_create(struct effect_manager *manager,
                                                   struct tap_ripple_effect_options *options,
                                                   const struct tap_ripple_effect_interface *impl,
                                                   const char *name, int priority, bool enabled,
                                                   void *user_data);

void tap_ripple_effect_add_point(struct tap_ripple_effect *effect, int32_t id, int32_t x,
                                 int32_t y);

void tap_ripple_effect_remove_all_points(struct tap_ripple_effect *effect);

struct trail_effect_options {
    float color[4];
    float thickness;
    uint32_t life_time;
};

struct trail_effect_interface {
    void (*enable)(struct trail_effect *base_effect, void *user_data);
    void (*disable)(struct trail_effect *base_effect, void *user_data);
    void (*destroy)(struct trail_effect *base_effect, void *user_data);
};

struct trail_effect *trail_effect_create(struct effect_manager *manager,
                                         struct trail_effect_options *options,
                                         const struct trail_effect_interface *impl,
                                         const char *name, int priority, bool enabled,
                                         void *user_data);

void trail_effect_add_trail(struct trail_effect *effect, int32_t id, int32_t start_x,
                            int32_t start_y);

void trail_effect_trail_add_point(struct trail_effect *effect, int32_t id, int32_t x, int32_t y);

void trail_effect_remove_all_trails(struct trail_effect *effect);

struct circle_progressbar_effect_options {
    int32_t size;
    uint32_t animate_delay;
    uint32_t animate_duration;
};

struct circle_progressbar_effect_interface {
    void (*enable)(struct circle_progressbar_effect *base_effect, void *user_data);
    void (*disable)(struct circle_progressbar_effect *base_effect, void *user_data);
    void (*destroy)(struct circle_progressbar_effect *base_effect, void *user_data);
};

struct circle_progressbar_effect *
circle_progressbar_effect_create(struct effect_manager *manager,
                                 struct circle_progressbar_effect_options *options,
                                 const struct circle_progressbar_effect_interface *impl,
                                 const char *name, int priority, bool enabled, void *user_data);

void circle_progressbar_effect_begin(struct circle_progressbar_effect *effect, int32_t x,
                                     int32_t y);

void circle_progressbar_effect_end(struct circle_progressbar_effect *effect);

#endif /* _EFFECT_SHAPE_H_ */
