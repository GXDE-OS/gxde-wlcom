// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _EFFECT_TRANSLATION_H_
#define _EFFECT_TRANSLATION_H_

#include "view/workspace.h"

struct workspace_translation;

bool workspace_add_automatic_translation_effect(struct workspace *current, struct workspace *next,
                                                enum direction direct);

struct workspace_translation *workspace_create_manual_translation_effect(struct workspace *center);

bool workspace_translation_manual(struct workspace_translation *ws_translation,
                                  enum direction direction, float offset);

struct workspace *workspace_translation_destroy(struct workspace_translation *ws_translation,
                                                float continue_switching_percent);

#endif /* _EFFECT_TRANSLATION_H_ */
