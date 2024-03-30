// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _SCENE_THUMBNAIL_H_
#define _SCENE_THUMBNAIL_H_

#include "scene.h"

struct server;

struct thumbnail_update_event {
    struct wlr_buffer *buffer;
    struct wlr_box content;
};

bool thumbnail_manager_create(struct server *server);

struct thumbnail *thumbnail_create_from_node(struct ky_scene_node *node, float scale);

void thumbnail_add_update_listener(struct thumbnail *thumbnail, struct wl_listener *listener);

void thumbnail_add_destroy_listener(struct thumbnail *thumbnail, struct wl_listener *listener);

void thumbnail_destroy(struct thumbnail *thumbnail);

#endif /* _SCENE_THUMBNAIL_H_ */
