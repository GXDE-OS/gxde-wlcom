// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _EFFECT_SLIDE_H_
#define _EFFECT_SLIDE_H_

#include "view/view.h"

#if HAVE_KDE_SLIDE
bool view_add_slide_effect(struct view *vew, bool mapped);

bool node_add_slide_effect(struct ky_scene_node *node, int location, int offset, bool slide_out);
#else
static __attribute__((unused)) inline bool view_add_slide_effect(struct view *vew, bool mapped)
{
    return false;
}

static __attribute__((unused)) inline bool
node_add_slide_effect(struct ky_scene_node *node, int location, int offset, bool slide_out)
{
    return false;
}
#endif

#endif /* _EFFECT_SLIDE_H_ */
