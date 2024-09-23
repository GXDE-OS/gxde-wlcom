// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _EFFECT_FADE_H_
#define _EFFECT_FADE_H_

#include "view/view.h"

enum fade_action {
    FADE_OUT = 0,
    FADE_IN,
};

bool view_add_fade_effect(struct view *view, enum fade_action action);

bool popup_add_fade_effect(struct ky_scene_node *node, enum fade_action action, bool topmost,
                           bool seat, float scale);

#endif /* _EFFECT_FADE_H_ */
