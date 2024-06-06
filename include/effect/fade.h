// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _EFFECT_FADE_H_
#define _EFFECT_FADE_H_

#include "view/view.h"

enum fade_action {
    FADE_MAP = 0,
    FADE_UNMAP,
};

bool view_add_fade_effect(struct view *view, enum fade_action action);

#endif /* _EFFECT_FADE_H_ */
