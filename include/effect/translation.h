// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _EFFECT_TRANSLATION_H_
#define _EFFECT_TRANSLATION_H_

#include "view/workspace.h"

bool workspace_add_translation_effect(struct workspace *current, struct workspace *next,
                                      enum direction direct);

#endif /* _EFFECT_TRANSLATION_H_ */
