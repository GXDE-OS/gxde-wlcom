// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _THEME_P_H_
#define _THEME_P_H_

#include "theme.h"

enum {
    BUTTONS_BUFFER = 0,
    CORNER_TOP_LEFT_ACTIVE_BUFFER,
    CORNER_TOP_LEFT_INACTIVE_BUFFER,
    CORNER_TOP_RIGHT_ACTIVE_BUFFER,
    CORNER_TOP_RIGHT_INACTIVE_BUFFER,
    THEME_BUFFER_COUNT,
};

struct theme_buffer {
    struct wl_list link;
    float scale;
    struct wlr_buffer *buf[THEME_BUFFER_COUNT];
};

struct theme_override {
    /* font config override by dbus */
    char *font_name;
    int32_t font_size;
};

struct theme_manager {
    struct wl_list themes;
    struct theme *current;
    struct theme_override override;
    struct config *config;

    struct {
        struct wl_signal update;
    } events;

    struct wl_listener server_destroy;
};

bool theme_manager_config_init(struct theme_manager *manager);

const char *theme_manager_read_config(struct theme_manager *manager);

void theme_manager_write_config(struct theme_manager *manager, const char *name);

#endif /* _THEME_P_H */
