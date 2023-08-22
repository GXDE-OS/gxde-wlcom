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
#include "scene/surface.h"
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

static void effect_handle_view_size_changed(struct wl_listener *listener, void *data)
{
    struct effect_view *_view = wl_container_of(listener, _view, view_handle_size_changed);
    if (_view->base.effects_state == EFFECTS_MAXIMIZING) {
        memcpy(&_view->base.dst_box, &_view->view->base.geometry, sizeof(_view->base.dst_box));
    }
}

static void effect_handle_view_pos_changed(struct wl_listener *listener, void *data)
{
    struct effect_view *_view = wl_container_of(listener, _view, view_handle_pos_changed);
    if (_view->base.effects_state == EFFECTS_MAXIMIZING) {
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
    effects_view->base.view_snap_buffer = NULL;

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

/* Release in effect_veiw destroy. */
static void effect_view_update_target(struct kywc_render_target *target, int x, int y, int width,
                                      int height)
{
    if (!target) {
        return;
    }

    kywc_gl_begin();
    int ofb = kywc_gl_get_current_framebuffer();
    struct kywc_gl_buffer *buffer = &target->buffer;
    kywc_gl_buffer_allocate(buffer, ofb, width, height);
    kywc_gl_end();

    target->lx = 0;
    target->ly = 0;
    target->scale = 1.0;
    target->current_ofb = ofb;
    target->wl_transform = 0;
    target->view_box.x = x;
    target->view_box.y = y;
    target->view_box.width = width;
    target->view_box.height = height;
}

/* Don't destroy the texture. Will be destroy with effect view destroy. */
struct kywc_gl_texture *kywc_effect_view_generate_texture(struct kywc_effect_view *view, int scale)
{
    struct effect_view *_view = _kywc_get_effect_view(view);
    if (!_view) {
        return NULL;
    }
    struct kywc_node *node = &view->view_node->node;
    struct wlr_box bound_box;
    node->get_bounding_box(node, &bound_box);
    int width = bound_box.width * scale;
    int height = bound_box.height * scale;

    effect_view_update_target(&_view->snap_target, bound_box.x, bound_box.y, width, height);

    kywc_target_render_begin(&_view->snap_target);
    glClearColor(0.0f, 0.f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    kywc_target_render_end(&_view->snap_target);

    pixman_region32_t damage;
    pixman_region32_init(&damage);
    pixman_region32_init_rect(&damage, 0, 0, width, height);

    kywc_scene_node_render(&view->view_node->node, &_view->snap_target, &damage);

    pixman_region32_fini(&damage);

    view->view_snap_buffer = &_view->snap_target.buffer;
    return _view->snap_target.buffer.fb_tex;
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
