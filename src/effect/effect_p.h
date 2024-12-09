// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _EFFECT_P_H_
#define _EFFECT_P_H_

#include "effect/effect.h"
#include "server.h"

struct effect_manager {
    struct wl_list effects;
    struct config *config;

    struct server *server;
    struct wl_listener server_destroy;
};

bool effect_manager_config_init(struct effect_manager *effect_manager);

bool effect_init_config(struct effect *effect);

bool showfps_effect_create(struct effect_manager *manager);

bool capture_manager_create(struct server *server);

bool ky_capture_manager_create(struct server *server);

bool move_effect_create(struct effect_manager *effect_manager);

bool blur_effect_create(struct effect_manager *effect_manager);

#if HAVE_UKUI_SCREENSHOT
bool screenshot_effect_create(struct effect_manager *effect_manager);
#else
static __attribute__((unused)) inline bool
screenshot_effect_create(struct effect_manager *effect_manager)
{
    return false;
}
#endif

#if HAVE_UKUI_WATERMARK
bool watermark_effect_create(struct effect_manager *manager);
#else
static __attribute__((unused)) inline bool
watermark_effect_create(struct effect_manager *effect_manager)
{
    return false;
}
#endif

bool scale_effect_create(struct effect_manager *manager);

bool soft_gamma_effect_create(struct effect_manager *manager);

bool touchclick_effect_create(struct effect_manager *manager);

bool touchtrail_effect_create(struct effect_manager *manager);

bool long_touch_effect_create(struct effect_manager *manager);

bool fade_effect_create(struct effect_manager *manager);

#if HAVE_KDE_SLIDE
bool slide_effect_create(struct effect_manager *manager);
#else
static __attribute__((unused)) inline bool
slide_effect_create(struct effect_manager *effect_manager)
{
    return false;
}
#endif

bool translation_effect_create(struct effect_manager *manager);

bool output_transform_effect_create(struct effect_manager *manager);

bool shake_cursor_effect_create(struct effect_manager *manager);

bool shake_view_effect_create(struct effect_manager *effect_manager);

bool locate_pointer_effect_create(struct effect_manager *manager);

bool magic_lamp_effect_create(struct effect_manager *manager);

#endif /* _EFFECT_P_H_ */
