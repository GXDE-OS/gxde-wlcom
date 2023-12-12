// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "scene/kycom/effect_view_impl.h"
#include "scene/kycom/effects_impl.h"

#include "kywc/kycom/scene.h"
#include "kywc/kycom/texture.h"
#include "kywc/kycom/transform.h"
#include "render/opengl.h"
#include "scene/surface.h"
#include "server.h"
#include "theme.h"
#include "view/view.h"

extern struct kywc_effect_view_impl impl;

struct effect_view *_kywc_get_effect_view(const struct kywc_effect_view *view)
{
    if (view) {
        assert(view->impl == &impl);
        return (struct effect_view *)view;
    }
    return NULL;
}

void kywc_effect_view_get_save_geometry(struct kywc_effect_view *view,
                                        struct kywc_box *geometry_box)
{
    struct effect_view *effect_view = _kywc_get_effect_view(view);
    if (!effect_view) {
        return;
    }
    struct view *_view = effect_view->view;
    geometry_box->x = _view->saved.geometry.x;
    geometry_box->y = _view->saved.geometry.y;
    geometry_box->width = _view->saved.geometry.width;
    geometry_box->height = _view->saved.geometry.height;
}

void kywc_effect_view_get_bound_box(struct kywc_effect_view *view, struct kywc_box *bound_box)
{
    if (!view || view->kywc_view->ssd != KYWC_SSD_ALL) {
        return;
    }
    struct kywc_node *node = &view->view_node->node;
    struct wlr_box box;
    node->get_bounding_box(node, &box);
    bound_box->x = box.x;
    bound_box->y = box.y;
    bound_box->width = box.width;
    bound_box->height = box.height;
}

static void effect_handle_view_size_changed(struct wl_listener *listener, void *data)
{
    struct effect_view *_view = wl_container_of(listener, _view, view_handle_size_changed);
    if (_view->base.effects_state == EFFECTS_ZOOMING) {
        memcpy(&_view->base.dst_box, &_view->view->base.geometry, sizeof(_view->base.dst_box));
    }
}

static void effect_handle_view_pos_changed(struct wl_listener *listener, void *data)
{
    struct effect_view *_view = wl_container_of(listener, _view, view_handle_pos_changed);
    if (_view->base.effects_state == EFFECTS_ZOOMING) {
        memcpy(&_view->base.dst_box, &_view->view->base.geometry, sizeof(_view->base.dst_box));
    }
}

static void effect_veiw_release_buffer(struct effect_view *_view)
{
    if (!_view) {
        return;
    }
    kywc_gl_begin();
    struct kywc_gl_buffer *buffer = &_view->snap_target.buffer;
    kywc_gl_buffer_release(buffer);
    kywc_gl_end();
}

static void effect_handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct effect_view *view = wl_container_of(listener, view, view_handle_destroy);

    wl_signal_emit_mutable(&view->base.events.destroy, &view->base);

    wl_list_remove(&view->view_handle_destroy.link);
    wl_list_remove(&view->view_handle_maximized.link);
    wl_list_remove(&view->view_handle_minimized.link);
    wl_list_remove(&view->view_handle_size_changed.link);
    wl_list_remove(&view->view_handle_pos_changed.link);
    wl_list_remove(&view->view_handle_map.link);

    effect_veiw_release_buffer(view);
    kywc_transform_root_destory(view->transform_root);

    free(view);
}

static void effect_view_handle_map(struct wl_listener *listener, void *data)
{
    struct effect_view *view = wl_container_of(listener, view, view_handle_map);

    wl_signal_emit_mutable(&view->base.events.map, &view->base);
}

static void effect_handle_view_maximize(struct wl_listener *listener, void *data)
{
    struct effect_view *effects_view =
        wl_container_of(listener, effects_view, view_handle_maximized);

    wl_signal_emit_mutable(&kywc_effect_server()->events.view_maximize, effects_view);
}

static void effect_handle_view_minimize(struct wl_listener *listener, void *data)
{
    struct effect_view *effects_view =
        wl_container_of(listener, effects_view, view_handle_minimized);

    wl_signal_emit_mutable(&kywc_effect_server()->events.view_minimize, effects_view);
}

void _kywc_effect_handle_view_create(struct wl_listener *listener, void *data)
{
    struct view *ky_view = wl_container_of(data, ky_view, base);

    _kywc_effect_view_create(ky_view);
}

struct kywc_effect_view *_kywc_effect_view_create(struct view *view)
{
    struct effect_view *effects_view = calloc(1, sizeof(struct effect_view));
    if (!effects_view) {
        return NULL;
    }
    effects_view->view = view;
    effects_view->base.kywc_view = &view->base;
    effects_view->surface_group = NULL;
    effects_view->transform_root = kywc_transform_root_create(&view->tree->node);
    effects_view->base.view_node = view->tree;

    kywc_gl_buffer_init(&effects_view->snap_target.buffer);

    effects_view->view_handle_destroy.notify = effect_handle_view_destroy;
    wl_signal_add(&view->base.events.destroy, &effects_view->view_handle_destroy);

    effects_view->view_handle_maximized.notify = effect_handle_view_maximize;
    wl_signal_add(&view->base.events.maximize, &effects_view->view_handle_maximized);

    effects_view->view_handle_minimized.notify = effect_handle_view_minimize;
    wl_signal_add(&view->base.events.minimize, &effects_view->view_handle_minimized);

    effects_view->view_handle_size_changed.notify = effect_handle_view_size_changed;
    wl_signal_add(&view->base.events.size, &effects_view->view_handle_size_changed);

    effects_view->view_handle_pos_changed.notify = effect_handle_view_pos_changed;
    wl_signal_add(&view->base.events.position, &effects_view->view_handle_pos_changed);

    effects_view->view_handle_map.notify = effect_view_handle_map;
    wl_signal_add(&view->base.events.map, &effects_view->view_handle_map);

    wl_signal_init(&effects_view->base.events.map);
    wl_signal_init(&effects_view->base.events.destroy);

    effects_view->base.impl = &impl;

    if (kywc_effect_server()) {
        wl_signal_emit_mutable(&(kywc_effect_server()->events.view_new), &effects_view->base);
    }
    return &effects_view->base;
}

struct kywc_render_target *kywc_effect_view_get_target(struct kywc_effect_view *view)
{
    struct effect_view *_view = _kywc_get_effect_view(view);
    return &_view->snap_target;
}

bool kywc_effect_view_is_minimized(struct kywc_effect_view *view)
{
    struct effect_view *effects_view = _kywc_get_effect_view(view);
    return effects_view->view->base.minimized;
}

static void effect_set_view_visible(struct effect_view *effects_view, bool visible)
{
    if (!effects_view) {
        return;
    }
    struct kywc_effect_view *view = &effects_view->base;
    kywc_node_set_enabled(&view->view_node->node, visible);
}

struct kywc_group_node *kywc_effect_view_get_transform(struct kywc_effect_view *effects_view,
                                                       const char *name)
{
    if (!effects_view || !effects_view->view_node) {
        return NULL;
    }
    return kywc_node_transform_get(&effects_view->view_node->node, name);
}

bool kywc_effect_view_add_transform(struct kywc_effect_view *effects_view,
                                    struct kywc_group_node *transform_node, const int z_order,
                                    const char *name)
{
    if (!effects_view || !effects_view->view_node) {
        return false;
    }

    return kywc_node_transform_add(&effects_view->view_node->node, transform_node, z_order, name);
}

struct kywc_group_node *kywc_effect_view_remove_transform(struct kywc_effect_view *effects_view,
                                                          const char *name)
{
    if (!effects_view || !effects_view->view_node) {
        return NULL;
    }
    return kywc_node_transform_remove(&effects_view->view_node->node, name);
}

struct kywc_texture_node *kywc_effect_view_get_texture_node(struct kywc_effect_view *view)
{
    struct effect_view *_view = _kywc_get_effect_view(view);
    struct kywc_texture_node *tex_node = ky_scene_buffer_try_from_surface(_view->view->surface);
    return tex_node;
}

struct kywc_texture_node *kywc_blur_get_texture_node(struct kde_blur *blur)
{
    if (!blur) {
        return NULL;
    }
    return ky_scene_buffer_try_from_surface(blur->wlr_surface);
}

static void set_view_visible(struct kywc_effect_view *view, bool visible)
{
    struct effect_view *effects_view = _kywc_get_effect_view(view);
    effect_set_view_visible(effects_view, visible);
}

struct kywc_effect_view_impl impl = {
    .effect_set_view_visible = set_view_visible,
};

void kywc_theme_manager_add_update_listener(struct wl_listener *listener)
{
    theme_manager_add_update_listener(listener);
}

void *kywc_theme_manager_get_current(const char *name)
{
    struct theme *th = theme_manager_get_current();
    if (strcmp(name, "border_width") == 0) {
        return &th->border_width;
    } else if (strcmp(name, "corner_radius") == 0) {
        return &th->corner_radius;
    } else if (strcmp(name, "active_border_color") == 0) {
        return th->active_border_color;
    } else if (strcmp(name, "inactive_border_color") == 0) {
        return th->inactive_border_color;
    }
    return NULL;
}

bool kywc_renderer_is_opengl(struct kywc_effect_server *server)
{
    struct wlr_renderer *renderer = server->kywc_server->renderer;
    return wlr_renderer_is_opengl(renderer);
}
