// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _EFFECT_SCALE_H_
#define _EFFECT_SCALE_H_

#include "view/view.h"

enum scale_action {
    SCALE_MAXIMIZE = 0,
    SCALE_MINIMIZE,
};

bool view_add_scale_effect(struct view *view, enum scale_action action);

#endif /* _EFFECT_SCALE_H_ */
