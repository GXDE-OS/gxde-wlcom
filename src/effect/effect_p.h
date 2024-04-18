// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

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

struct effect *effect_by_uuid(const char *uuid);

struct effect *effect_by_name(const char *name);

bool showfps_effect_create(struct effect_manager *manager);

bool capture_manager_create(struct server *server);

bool ky_capture_manager_create(struct server *server);

#if HAVE_UKUI_SCREENSHOT
bool ukui_screenshot_create(struct effect_manager *effect_manager);
#else
static __attribute__((unused)) inline bool
ukui_screenshot_create(struct effect_manager *effect_manager)
{
    return false;
}
#endif

#endif /* _EFFECT_P_H_ */
