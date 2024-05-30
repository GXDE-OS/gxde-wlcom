// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>

#include <wlr/types/wlr_buffer.h>

#include "effect/move.h"
#include "effect_p.h"
#include "scene/box.h"
#include "scene/thumbnail.h"

enum move_effect_type {
    MOVE_EFFECT_BORDER = 0, // no window content shown
    MOVE_EFFECT_OPACITY,
};

struct move_proxy {
    struct wl_list link;

    struct view *view;
    struct ky_scene_node *node;
    /* MOVE_EFFECT_BORDER */
    struct ky_scene_box *box;
    /* MOVE_EFFECT_OPACITY */
    struct ky_scene_buffer *buffer;
    struct thumbnail *thumbnail;
    struct wl_listener thumbnail_update;
    struct wl_listener thumbnail_destroy;

    struct {
        struct wl_signal destroy;
    } events;

    int x, y, width, height;
};

static struct move_effect {
    struct wl_list proxies;

    struct effect *effect;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;

    enum move_effect_type type;
} *effect = NULL;

static void proxy_handle_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct move_proxy *proxy = wl_container_of(listener, proxy, thumbnail_update);
    struct thumbnail_update_event *event = data;
    ky_scene_buffer_set_dest_size(proxy->buffer, event->buffer->width, event->buffer->height);
    ky_scene_buffer_set_buffer(proxy->buffer, event->buffer);
}

static void proxy_handle_thumbnail_destroy(struct wl_listener *listener, void *data)
{
    struct move_proxy *proxy = wl_container_of(listener, proxy, thumbnail_destroy);
    proxy->thumbnail = NULL;
    move_proxy_destroy(proxy);
}

static void proxy_create_node(struct move_proxy *proxy)
{
    struct ky_scene_tree *tree = proxy->view->tree->node.parent;

    if (effect->type == MOVE_EFFECT_BORDER) {
        float color[4] = { 0.5, 0.5, 0.5, 1.0 };
        proxy->box = ky_scene_box_create(tree, proxy->width, proxy->height, color, 1);
        proxy->node = ky_scene_node_from_box(proxy->box);
    } else if (effect->type == MOVE_EFFECT_OPACITY) {
        proxy->buffer = ky_scene_buffer_create(tree, NULL);
        proxy->node = &proxy->buffer->node;
        ky_scene_buffer_set_opacity(proxy->buffer, 0.5);

        proxy->thumbnail = thumbnail_create_from_view(proxy->view, THUMBNAIL_DISABLE_SHADOW, 1.0);
        proxy->thumbnail_update.notify = proxy_handle_thumbnail_update;
        thumbnail_add_update_listener(proxy->thumbnail, &proxy->thumbnail_update);
        proxy->thumbnail_destroy.notify = proxy_handle_thumbnail_destroy;
        thumbnail_add_destroy_listener(proxy->thumbnail, &proxy->thumbnail_destroy);
        // FIXME: thumbnail update is disable here, using node effect instead
        ky_scene_node_set_enabled(&proxy->view->tree->node, false);
    }
}

void move_proxy_move(struct move_proxy *proxy, int x, int y)
{
    proxy->x = x;
    proxy->y = y;

    if (!proxy->node) {
        proxy_create_node(proxy);
    }

    ky_scene_node_set_position(proxy->node, x - proxy->view->base.margin.off_x,
                               y - proxy->view->base.margin.off_y);
}

void move_proxy_resize(struct move_proxy *proxy, int width, int height)
{
    proxy->width = width;
    proxy->height = height;

    if (proxy->box) {
        ky_scene_box_set_size(proxy->box, width, height);
    }
}

void move_proxy_destroy(struct move_proxy *proxy)
{
    wl_signal_emit_mutable(&proxy->events.destroy, NULL);

    wl_list_remove(&proxy->link);
    wl_list_remove(&proxy->thumbnail_update.link);
    wl_list_remove(&proxy->thumbnail_destroy.link);

    if (proxy->node) {
        kywc_view_move(&proxy->view->base, proxy->x, proxy->y);
        ky_scene_node_destroy(proxy->node);
    }

    if (proxy->thumbnail) {
        thumbnail_destroy(proxy->thumbnail);
        ky_scene_node_set_enabled(&proxy->view->tree->node, true);
    }

    free(proxy);
}

struct move_proxy *move_proxy_create(struct view *view, int width, int height)
{
    if (!effect || !effect->effect->enabled) {
        return NULL;
    }

    struct move_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy) {
        return NULL;
    }

    proxy->view = view;
    proxy->width = width;
    proxy->height = height;
    wl_signal_init(&proxy->events.destroy);
    wl_list_init(&proxy->thumbnail_update.link);
    wl_list_init(&proxy->thumbnail_destroy.link);
    wl_list_insert(&effect->proxies, &proxy->link);

    return proxy;
}

void move_proxy_add_destroy_listener(struct move_proxy *proxy, struct wl_listener *listener)
{
    wl_signal_add(&proxy->events.destroy, listener);
}

static void handle_effect_enable(struct wl_listener *listener, void *data)
{
    // do nothing as we don't create proxy when effect is disabled
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct move_proxy *proxy, *tmp;
    wl_list_for_each_safe(proxy, tmp, &effect->proxies, link) {
        move_proxy_destroy(proxy);
    }
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    assert(wl_list_empty(&effect->proxies));
    wl_list_remove(&effect->destroy.link);
    wl_list_remove(&effect->enable.link);
    wl_list_remove(&effect->disable.link);
    free(effect);
    effect = NULL;
}

bool move_effect_create(struct effect_manager *effect_manager)
{
    effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }

    effect->effect = effect_create("move", 0, false, NULL);
    if (!effect->effect) {
        free(effect);
        effect = NULL;
        return false;
    }

    wl_list_init(&effect->proxies);
    effect->type = MOVE_EFFECT_BORDER;

    effect->enable.notify = handle_effect_enable;
    wl_signal_add(&effect->effect->events.enable, &effect->enable);
    effect->disable.notify = handle_effect_disable;
    wl_signal_add(&effect->effect->events.disable, &effect->disable);
    effect->destroy.notify = handle_effect_destroy;
    wl_signal_add(&effect->effect->events.destroy, &effect->destroy);

    return true;
}
