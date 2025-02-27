// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _SCENE_SURFACE_H_
#define _SCENE_SURFACE_H_

#include "scene.h"

enum slide_location {
    SLIDE_LOCATION_LEFT = 0,
    SLIDE_LOCATION_TOP = 1,
    SLIDE_LOCATION_RIGHT = 2,
    SLIDE_LOCATION_BOTTOM = 3,
};

struct slide {
    enum slide_location location;
    int offset;
};

struct ky_scene_surface {
    struct ky_scene_buffer *buffer;
    struct wlr_surface *surface;

    struct wlr_addon addon;
    struct wlr_addon node_addon;

    struct wl_listener outputs_update;
    struct wl_listener output_enter;
    struct wl_listener output_leave;
    struct wl_listener output_sample;
    struct wl_listener frame_done;
    struct wl_listener surface_destroy;
    struct wl_listener surface_commit;
    struct wl_listener surface_map;

    struct ky_scene_node_interface buffer_default_impl;
};

struct ky_scene_tree *ky_scene_subsurface_tree_create(struct ky_scene_tree *parent,
                                                      struct wlr_surface *surface);

struct ky_scene_surface *ky_scene_surface_create(struct ky_scene_tree *parent,
                                                 struct wlr_surface *wlr_surface);

struct ky_scene_surface *ky_scene_surface_try_from_buffer(struct ky_scene_buffer *scene_buffer);

struct wlr_surface *wlr_surface_try_from_node(struct ky_scene_node *node);

struct ky_scene_buffer *ky_scene_buffer_try_from_surface(struct wlr_surface *wlr_surface);

#endif /* _SCENE_SURFACE_H_ */
