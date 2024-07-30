// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _SCENE_THUMBNAIL_H_
#define _SCENE_THUMBNAIL_H_

#include "scene.h"
#include "view/view.h"
#include "view/workspace.h"

struct server;
struct kywc_output_state;

struct thumbnail_update_event {
    struct wlr_buffer *buffer;
    bool buffer_changed;
};

enum thumbnail_option {
    /* disable server decoration, include shadow */
    THUMBNAIL_DISABLE_DECOR = 1 << 0,
    /* disable shadow only if has it */
    THUMBNAIL_DISABLE_SHADOW = 1 << 1,
    /* disable round corners */
    THUMBNAIL_DISABLE_ROUND_CORNER = 1 << 2,
    /* prefer single plane buffer */
    THUMBNAIL_ENABLE_SINGLE_PLANE = 1 << 3,
    /* enable security check */
    THUMBNAIL_ENABLE_SECURITY = 1 << 4,
};

bool thumbnail_manager_create(struct server *server);

struct thumbnail *thumbnail_create_from_node(struct ky_scene_node *node, float scale);

struct thumbnail *thumbnail_create_from_view(struct view *view, uint32_t option, float scale);

struct thumbnail *thumbnail_create_from_workspace(struct workspace *workspace,
                                                  struct kywc_output *output, float scale,
                                                  bool single_plane);

struct thumbnail *thumbnail_create_from_output(struct ky_scene_output *output,
                                               struct kywc_output_state *output_state, float scale);

void thumbnail_add_update_listener(struct thumbnail *thumbnail, struct wl_listener *listener);

void thumbnail_add_destroy_listener(struct thumbnail *thumbnail, struct wl_listener *listener);

void thumbnail_mark_wants_update(struct thumbnail *thumbnail, bool wants);

/* logic coord */
bool thumbnail_get_node_offset(struct thumbnail *thumbnail, struct ky_scene_node *node, int32_t *x,
                               int32_t *y);

void thumbnail_update(struct thumbnail *thumbnail);

void thumbnail_destroy(struct thumbnail *thumbnail);

#endif /* _SCENE_THUMBNAIL_H_ */
