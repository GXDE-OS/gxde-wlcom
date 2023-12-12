// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECT_VIEW_H_
#define _EFFECT_VIEW_H_

#include <pixman.h>

#include "kywc/view.h"

#include "effects.h"

struct kywc_group_node;
struct kywc_effect_view;

struct padding {
    int top, bottom, left, right;
};

enum kywc_effects_state {
    EFFECTS_END = 0,
    EFFECTS_ZOOMING = 1 << 0,
};

struct kywc_effect_view_impl {
    void (*effect_set_view_visible)(struct kywc_effect_view *view, bool visible);
};

struct kywc_effect_view {
    struct kywc_effect_view_impl *impl;
    struct kywc_view *kywc_view;
    struct kywc_group_node *view_node;
    struct kywc_effect_box dst_box;
    struct kywc_effect_box last_box;
    enum kywc_effects_state effects_state;

    struct {
        /* data is kywc_effect_view. */
        struct wl_signal map;
        struct wl_signal destroy;
    } events;
};

struct kywc_group_node *kywc_effect_view_get_transform(struct kywc_effect_view *effects_view,
                                                       const char *name);

bool kywc_effect_view_add_transform(struct kywc_effect_view *effects_view,
                                    struct kywc_group_node *transform_node, const int z_order,
                                    const char *name);

struct kywc_group_node *kywc_effect_view_remove_transform(struct kywc_effect_view *effects_view,
                                                          const char *name);

struct kywc_texture_node *kywc_effect_view_get_texture_node(struct kywc_effect_view *view);

struct kywc_render_target *kywc_effect_view_get_target(struct kywc_effect_view *view);

/*************************************************************************/
void kywc_theme_manager_add_update_listener(struct wl_listener *listener);

void *kywc_theme_manager_get_current(const char *name);

/*************************************************************************/
struct kde_blur {
    struct wl_list link;
    struct wl_list resources;

    struct wlr_surface *wlr_surface;
    struct wl_listener surface_map;
    struct wl_listener surface_destroy;

    pixman_region32_t region, pending_region;
    uint32_t strength, pending_strength;

    struct {
        struct wl_signal commit;
    } events;
};

struct kywc_texture_node *kywc_blur_get_texture_node(struct kde_blur *blur);

bool kywc_effect_view_is_minimized(struct kywc_effect_view *view);

void kywc_effect_view_get_save_geometry(struct kywc_effect_view *view,
                                        struct kywc_box *geometry_box);

void kywc_effect_view_get_bound_box(struct kywc_effect_view *view, struct kywc_box *bound_box);

bool kywc_renderer_is_opengl(struct kywc_effect_server *server);

#endif
