// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include "kywc/kycom/effect_addon.h"

void kywc_effect_addon_init(struct wlr_addon *addon, struct wlr_addon_set *set, const void *owner,
                            const struct wlr_addon_interface *impl)
{
    wlr_addon_init(addon, set, owner, impl);
}

void kywc_effect_addon_finish(struct wlr_addon *addon)
{
    wlr_addon_finish(addon);
}

struct wlr_addon *kywc_effect_addon_find(struct wlr_addon_set *set, const void *owner,
                                         const struct wlr_addon_interface *impl)
{
    return wlr_addon_find(set, owner, impl);
}
