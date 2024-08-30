// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _EFFECT_FADE_H_
#define _EFFECT_FADE_H_

#include "effect/animator.h"
#include "view/view.h"

enum fade_action {
    FADE_OUT = 0,
    FADE_IN,
};

struct fade_options {
    struct ky_scene_node *entity_node;
    struct animation_type_group type;
    enum fade_action action;
    float thumbnail_scale;
    int64_t start_time;
    int duration;
    float factor;
    int offset;
};

bool view_add_fade_effect(struct view *view, enum fade_action action);

bool popup_add_fade_effect(struct ky_scene_node *entity_node, struct ky_scene_node *node,
                           enum fade_action action, bool topmost, bool seat, float scale);

bool node_add_fade_effect(struct ky_scene_node *node, struct fade_options *options);

#endif /* _EFFECT_FADE_H_ */