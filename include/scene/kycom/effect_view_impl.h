// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECT_VIEW_IMPL_H_
#define _EFFECT_VIEW_IMPL_H_

#include "kywc/kycom/target.h"
#include "kywc/kycom/effect_view.h"

struct effect_view {
    struct kywc_effect_view base;
    struct view *view;
    struct wl_list link;

    struct kywc_transform_root_node *transform_root;
    struct kywc_group_node *surface_group;
    struct kywc_render_target snap_target;

    struct wl_listener view_handle_maximized;
    struct wl_listener view_handle_minimized;

    struct wl_listener view_handle_size_changed;
    struct wl_listener view_handle_pos_changed;
    struct wl_listener view_handle_destroy;
    struct wl_listener view_handle_map;
};

struct kywc_effect_view *_kywc_effect_view_create(struct view *view);

void _kywc_effect_handle_view_create(struct wl_listener *listener, void *data);

struct effect_view *_kywc_get_effect_view(const struct kywc_effect_view *view);

#endif
