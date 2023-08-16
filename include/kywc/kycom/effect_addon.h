// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECT_ADDON_H_
#define _EFFECT_ADDON_H_

#include <wlr/util/addon.h>

void kywc_effect_addon_init(struct wlr_addon *addon, struct wlr_addon_set *set, const void *owner,
                            const struct wlr_addon_interface *impl);

void kywc_effect_addon_finish(struct wlr_addon *addon);

struct wlr_addon *kywc_effect_addon_find(struct wlr_addon_set *set, const void *owner,
                                         const struct wlr_addon_interface *impl);

#endif
