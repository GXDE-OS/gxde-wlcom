// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _EFFECT_SHAPE_H_
#define _EFFECT_SHAPE_H_

#include <stdbool.h>
#include <stdint.h>

struct effect_manager;
struct circle_progressbar_effect;

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
