// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>

#include <drm_fourcc.h>
#include <wlr/render/allocator.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>

#include "scene/thumbnail.h"
#include "scene_p.h"
#include "server.h"

struct thumbnail {
    struct wlr_buffer *buffer;

    float scale;
    size_t ref_count;
    bool need_refresh; // need refresh or redrawn
    bool need_destroy; // need force destroyed

    struct {
        struct wl_signal update; // thumbnail_update_event
        struct wl_signal destroy;
    } events;

    bool (*render)(struct thumbnail *thumbnail, struct ky_scene_output *output);
    void (*destroy)(struct thumbnail *thumbnail);
};

struct node_thumbnail {
    struct thumbnail base;
    struct wl_list link;

    struct ky_scene_node *source_node;
    struct wl_listener source_damage;
    struct wl_listener source_destroy;
};

static struct thumbnail_manager {
    struct wl_list node_thumbnails;

    /* pick one output to render all thumbnails */
    struct ky_scene_output *output;
    struct wl_listener output_frame;
    struct wl_listener output_destroy;
    bool frame_pending;

    struct server *server;
    struct wl_listener destroy;
} *manager = NULL;

static void thumbnail_manager_schedule_frame(void);

static void thumbnail_init(struct thumbnail *thumbnail, float scale)
{
    thumbnail->scale = scale;
    thumbnail->ref_count = 1;
    thumbnail->need_refresh = true;

    wl_signal_init(&thumbnail->events.destroy);
    wl_signal_init(&thumbnail->events.update);
}

static struct node_thumbnail *find_node_thumbnail(struct ky_scene_node *node, float scale)
{
    struct node_thumbnail *node_thumbnail;
    wl_list_for_each(node_thumbnail, &manager->node_thumbnails, link) {
        if (node_thumbnail->source_node == node && node_thumbnail->base.scale == scale) {
            return node_thumbnail;
        }
    }
    return NULL;
}

static struct wlr_buffer *thumbnail_buffer_allocate(struct thumbnail *thumbnail, int width,
                                                    int height, struct wlr_allocator *allocator)
{
    bool need_create = !thumbnail->buffer || // no buffer or smaller than source
                       (thumbnail->buffer->width < width || thumbnail->buffer->height < height);
    if (!need_create) {
        return thumbnail->buffer;
    }

    uint64_t modifier[2] = { DRM_FORMAT_MOD_LINEAR, DRM_FORMAT_MOD_INVALID };
    struct wlr_drm_format format = { DRM_FORMAT_ARGB8888, 2, 2, modifier };
    struct wlr_buffer *buffer = wlr_allocator_create_buffer(allocator, width, height, &format);
    if (!buffer) {
        kywc_log(KYWC_ERROR, "failed create wlr buffer");
        return NULL;
    }

    return buffer;
}

static bool node_thumbnail_render(struct thumbnail *thumbnail, struct ky_scene_output *scene_output)
{
    struct node_thumbnail *node_thumbnail = wl_container_of(thumbnail, node_thumbnail, base);
    struct ky_scene_node *source_node = node_thumbnail->source_node;
    struct wlr_box bounding_box = { 0 };
    source_node->impl.get_bounding_box(source_node, &bounding_box);

    // TODO: round ?
    int buffer_width = bounding_box.width * thumbnail->scale;
    int buffer_height = bounding_box.height * thumbnail->scale;

    struct wlr_buffer *buffer = thumbnail_buffer_allocate(thumbnail, buffer_width, buffer_height,
                                                          scene_output->output->allocator);
    if (!buffer) {
        return false;
    }

    struct wlr_render_pass *render_pass =
        wlr_renderer_begin_buffer_pass(scene_output->output->renderer, buffer, NULL);
    if (!render_pass) {
        wlr_buffer_drop(buffer);
        return false;
    }

    /* clear the target buffer */
    wlr_render_pass_add_rect(render_pass, &(struct wlr_render_rect_options){
                                              .color = { 0, 0, 0, 0 },
                                              .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
                                          });

    struct ky_scene_render_target target = {
        .logical = { 0, 0, buffer_width, buffer_height },
        .scale = thumbnail->scale,

        .trans_width = buffer_width,
        .trans_height = buffer_height,

        .buffer = buffer,
        .output = scene_output,
        .render_pass = render_pass,

        .options = KY_SCENE_RENDER_DISABLE_VISIBILITY,
    };
    pixman_region32_init_rect(&target.damage, 0, 0, bounding_box.width, bounding_box.height);

    bool old_state = source_node->enabled;
    source_node->enabled = true;
    source_node->impl.render(source_node, -bounding_box.x, -bounding_box.y, &target);
    source_node->enabled = old_state;
    wlr_render_pass_submit(target.render_pass);
    pixman_region32_fini(&target.damage);

    if (buffer != thumbnail->buffer) {
        if (thumbnail->buffer) {
            wlr_buffer_drop(thumbnail->buffer);
        }
        thumbnail->buffer = buffer;
    }

    struct thumbnail_update_event event = {
        .buffer = buffer,
        .content = { 0, 0, buffer_width, buffer_height },
    };
    wl_signal_emit_mutable(&thumbnail->events.update, &event);

    return true;
}

static void node_thumbnail_destroy(struct thumbnail *thumbnail)
{
    if (thumbnail->buffer) {
        wlr_buffer_drop(thumbnail->buffer);
    }

    struct node_thumbnail *node_thumbnail = wl_container_of(thumbnail, node_thumbnail, base);
    wl_list_remove(&node_thumbnail->source_destroy.link);
    wl_list_remove(&node_thumbnail->source_damage.link);
    wl_list_remove(&node_thumbnail->link);

    free(thumbnail);
}

static void node_thumbnail_handle_source_destroy(struct wl_listener *listener, void *data)
{
    struct node_thumbnail *node_thumbnail =
        wl_container_of(listener, node_thumbnail, source_destroy);
    /* force destroyed when source node destroy */
    node_thumbnail->base.need_destroy = true;
    thumbnail_destroy(&node_thumbnail->base);
}

static void node_thumbnail_handle_source_damage(struct wl_listener *listener, void *data)
{
    struct node_thumbnail *node_thumbnail =
        wl_container_of(listener, node_thumbnail, source_damage);
    node_thumbnail->base.need_refresh = true;
    thumbnail_manager_schedule_frame();
}

struct thumbnail *thumbnail_create_from_node(struct ky_scene_node *node, float scale)
{
    if (!manager) {
        return NULL;
    }

    struct node_thumbnail *node_thumbnail = find_node_thumbnail(node, scale);
    if (node_thumbnail) {
        node_thumbnail->base.ref_count++;
        return &node_thumbnail->base;
    }

    node_thumbnail = calloc(1, sizeof(*node_thumbnail));
    if (!node_thumbnail) {
        return NULL;
    }

    thumbnail_init(&node_thumbnail->base, scale);
    node_thumbnail->base.render = node_thumbnail_render;
    node_thumbnail->base.destroy = node_thumbnail_destroy;

    node_thumbnail->source_node = node;
    node_thumbnail->source_damage.notify = node_thumbnail_handle_source_damage;
    wl_signal_add(&node->events.damage, &node_thumbnail->source_damage);
    node_thumbnail->source_destroy.notify = node_thumbnail_handle_source_destroy;
    wl_signal_add(&node->events.destroy, &node_thumbnail->source_destroy);

    wl_list_insert(&manager->node_thumbnails, &node_thumbnail->link);

    thumbnail_manager_schedule_frame();

    return &node_thumbnail->base;
}

void thumbnail_destroy(struct thumbnail *thumbnail)
{
    if (!thumbnail) {
        return;
    }

    assert(thumbnail->ref_count > 0);
    thumbnail->ref_count--;

    if (thumbnail->ref_count == 0 || thumbnail->need_destroy) {
        wl_signal_emit_mutable(&thumbnail->events.destroy, NULL);
        thumbnail->destroy(thumbnail);
    }
}

void thumbnail_add_update_listener(struct thumbnail *thumbnail, struct wl_listener *listener)
{
    wl_signal_add(&thumbnail->events.update, listener);
}

void thumbnail_add_destroy_listener(struct thumbnail *thumbnail, struct wl_listener *listener)
{
    wl_signal_add(&thumbnail->events.destroy, listener);
}

static void thumbnail_manager_handle_output_frame(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->output_frame.link);
    wl_list_init(&manager->output_frame.link);
    manager->frame_pending = false;

    struct node_thumbnail *node_thumbnail, *tmp;
    wl_list_for_each_safe(node_thumbnail, tmp, &manager->node_thumbnails, link) {
        if (!node_thumbnail->base.need_refresh) {
            continue;
        }
        node_thumbnail->base.need_refresh = false;
        /* destroy it when render failed */
        if (!node_thumbnail->base.render(&node_thumbnail->base, manager->output)) {
            node_thumbnail->base.need_destroy = true;
            thumbnail_destroy(&node_thumbnail->base);
        }
    }
}

static void thumbnail_manager_handle_output_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->output_destroy.link);
    wl_list_remove(&manager->output_frame.link);
    manager->output = NULL;
    manager->frame_pending = false;

    thumbnail_manager_schedule_frame();
}

static void thumbnail_manager_update_output(void)
{
    struct ky_scene *scene = manager->server->scene;
    if (wl_list_empty(&scene->outputs)) {
        return;
    }

    manager->output = wl_container_of(scene->outputs.next, manager->output, link);
    wl_list_init(&manager->output_frame.link);
    wl_signal_add(&manager->output->events.destroy, &manager->output_destroy);
}

static void thumbnail_manager_schedule_frame(void)
{
    if (manager->frame_pending) {
        return;
    }

    if (manager->output == NULL) {
        thumbnail_manager_update_output();
        if (manager->output == NULL) {
            return;
        }
    }

    manager->frame_pending = true;
    wl_signal_add(&manager->output->events.frame, &manager->output_frame);
    wlr_output_schedule_frame(manager->output->output);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    free(manager);
    manager = NULL;
}

bool thumbnail_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    wl_list_init(&manager->node_thumbnails);
    manager->output_frame.notify = thumbnail_manager_handle_output_frame;
    manager->output_destroy.notify = thumbnail_manager_handle_output_destroy;

    manager->server = server;
    manager->destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->destroy);

    return true;
}
