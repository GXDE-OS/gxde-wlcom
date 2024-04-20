// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <drm_fourcc.h>
#include <wlr/types/wlr_output.h>

#include <kywc/log.h>

#include "render/renderer.h"
#include "scene/render.h"
#include "scene/thumbnail.h"
#include "server.h"
#include "theme.h"

struct thumbnail {
    struct wl_list link;
    struct thumbnail_buffer *buffer;

    struct {
        struct wl_signal update; // thumbnail_update_event
        struct wl_signal destroy;
    } events;

    bool newly_added, wants_update;
};

struct thumbnail_buffer {
    struct wlr_buffer *buffer;
    struct wl_list thumbnails;

    float scale;
    bool was_damaged;  // buffer is damaged
    bool need_destroy; // need force destroyed
    bool can_destroy;  // cannot be destroyed when render

    struct wlr_buffer *(*render)(struct thumbnail_buffer *buffer, struct ky_scene_output *output);
    void (*destroy)(struct thumbnail_buffer *buffer);
};

struct node_thumbnail {
    struct thumbnail_buffer base;
    struct wl_list link;

    struct ky_scene_node *source_node;
    struct wl_listener source_damage;
    struct wl_listener source_destroy;
};

struct view_thumbnail {
    struct thumbnail_buffer base;
    struct wl_list link;
    uint32_t option;

    struct view *view;
    struct wl_listener view_unmap;
    struct ky_scene_node *source_node;
    struct wl_listener source_damage;
};

static struct thumbnail_manager {
    struct wl_list node_thumbnails;
    struct wl_list view_thumbnails;

    /* pick one output to render all thumbnails */
    struct ky_scene_output *output;
    struct wl_listener output_frame;
    struct wl_listener output_destroy;
    bool frame_pending;

    struct server *server;
    struct wl_listener destroy;
} *manager = NULL;

static void thumbnail_manager_schedule_frame(void);

static void thumbnail_buffer_init(struct thumbnail_buffer *buffer, float scale)
{
    buffer->scale = scale;
    buffer->was_damaged = true;
    buffer->can_destroy = true;

    wl_list_init(&buffer->thumbnails);
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

static struct view_thumbnail *find_view_thumbnail(struct view *view, uint32_t option, float scale)
{
    struct view_thumbnail *view_thumbnail;
    wl_list_for_each(view_thumbnail, &manager->view_thumbnails, link) {
        if (view_thumbnail->view == view && view_thumbnail->option == option &&
            view_thumbnail->base.scale == scale) {
            return view_thumbnail;
        }
    }
    return NULL;
}

static void view_thumbnail_get_box(struct view_thumbnail *view_thumbnail, struct wlr_box *box)
{
    struct kywc_view *kywc_view = &view_thumbnail->view->base;

    box->x = box->y = 0;
    box->width = kywc_view->geometry.width;
    box->height = kywc_view->geometry.height;

    if (kywc_view->ssd == KYWC_SSD_NONE) {
        box->x -= kywc_view->padding.left;
        box->y -= kywc_view->padding.top;
        box->width += kywc_view->padding.right;
        box->height += kywc_view->padding.bottom;
        return;
    }

    uint32_t option = view_thumbnail->option;
    struct theme *theme = theme_manager_get_current();
    if (option & THUMBNAIL_DISABLE_DECOR) {
        return;
    } else if (option & THUMBNAIL_DISABLE_SHADOW) {
        box->x -= kywc_view->margin.off_x;
        box->y -= kywc_view->margin.off_y;
        box->width += kywc_view->margin.off_width * 2;
        box->height += kywc_view->margin.off_height * 2;
    } else if (option & THUMBNAIL_DISABLE_ROUND_CORNER || option == 0) {
        box->x -= kywc_view->margin.off_x + theme->ssd.shadow_border;
        box->y -= kywc_view->margin.off_y + theme->ssd.shadow_border;
        box->width += kywc_view->margin.off_width + theme->ssd.shadow_border * 2;
        box->height += kywc_view->margin.off_height + theme->ssd.shadow_border * 2;
    }
}

static struct wlr_buffer *thumbnail_buffer_allocate(struct thumbnail_buffer *thumbnail_buffer,
                                                    int width, int height,
                                                    struct wlr_allocator *allocator)
{
    bool change = !thumbnail_buffer->buffer || (thumbnail_buffer->buffer->width != width ||
                                                thumbnail_buffer->buffer->height != height);
    if (!change) {
        return thumbnail_buffer->buffer;
    }

    struct wlr_buffer *buffer = ky_renderer_create_buffer(
        manager->server->renderer, manager->server->allocator, width, height, DRM_FORMAT_ARGB8888);
    if (!buffer) {
        kywc_log(KYWC_ERROR, "failed create wlr buffer");
        return NULL;
    }

    return buffer;
}

static struct wlr_buffer *node_thumbnail_render(struct thumbnail_buffer *thumbnail_buffer,
                                                struct ky_scene_output *scene_output)
{
    struct node_thumbnail *node_thumbnail = wl_container_of(thumbnail_buffer, node_thumbnail, base);
    struct ky_scene_node *source_node = node_thumbnail->source_node;
    struct wlr_box bounding_box = { 0 };
    source_node->impl.get_bounding_box(source_node, &bounding_box);

    /* bounding_box become empty when some client is minimized */
    if (wlr_box_empty(&bounding_box)) {
        return NULL;
    }

    int buffer_width = bounding_box.width * thumbnail_buffer->scale;
    int buffer_height = bounding_box.height * thumbnail_buffer->scale;

    struct wlr_buffer *buffer = thumbnail_buffer_allocate(
        thumbnail_buffer, buffer_width, buffer_height, scene_output->output->allocator);
    if (!buffer) {
        return NULL;
    }

    struct wlr_render_pass *render_pass =
        wlr_renderer_begin_buffer_pass(scene_output->output->renderer, buffer, NULL);
    if (!render_pass) {
        wlr_buffer_drop(buffer);
        return NULL;
    }

    /* clear the target buffer */
    wlr_render_pass_add_rect(render_pass, &(struct wlr_render_rect_options){
                                              .color = { 0, 0, 0, 0 },
                                              .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
                                          });

    struct ky_scene_render_target target = {
        .logical = { 0, 0, buffer_width, buffer_height },
        .scale = thumbnail_buffer->scale,
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

    return buffer;
}

static struct wlr_buffer *view_thumbnail_render(struct thumbnail_buffer *thumbnail_buffer,
                                                struct ky_scene_output *scene_output)
{
    struct view_thumbnail *view_thumbnail = wl_container_of(thumbnail_buffer, view_thumbnail, base);
    struct wlr_box bounding_box = { 0 };

    view_thumbnail_get_box(view_thumbnail, &bounding_box);

    int buffer_width = bounding_box.width * thumbnail_buffer->scale;
    int buffer_height = bounding_box.height * thumbnail_buffer->scale;

    struct wlr_buffer *buffer = thumbnail_buffer_allocate(
        thumbnail_buffer, buffer_width, buffer_height, scene_output->output->allocator);
    if (!buffer) {
        return NULL;
    }

    struct wlr_render_pass *render_pass =
        wlr_renderer_begin_buffer_pass(scene_output->output->renderer, buffer, NULL);
    if (!render_pass) {
        wlr_buffer_drop(buffer);
        return NULL;
    }

    /* clear the target buffer */
    wlr_render_pass_add_rect(render_pass, &(struct wlr_render_rect_options){
                                              .color = { 0, 0, 0, 0 },
                                              .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
                                          });

    struct ky_scene_render_target target = {
        .logical = { 0, 0, buffer_width, buffer_height },
        .scale = thumbnail_buffer->scale,
        .trans_width = buffer_width,
        .trans_height = buffer_height,
        .buffer = buffer,
        .output = scene_output,
        .render_pass = render_pass,
        .options = KY_SCENE_RENDER_DISABLE_VISIBILITY,
    };
    if (view_thumbnail->option & THUMBNAIL_DISABLE_ROUND_CORNER) {
        target.options |= KY_SCENE_RENDER_DISABLE_ROUND_CORNER;
    }

    pixman_region32_init_rect(&target.damage, 0, 0, bounding_box.width, bounding_box.height);

    struct ky_scene_node *source_node = view_thumbnail->source_node;
    bool old_state = source_node->enabled;
    source_node->enabled = true;
    source_node->impl.render(source_node, -bounding_box.x, -bounding_box.y, &target);
    source_node->enabled = old_state;
    wlr_render_pass_submit(target.render_pass);
    pixman_region32_fini(&target.damage);

    return buffer;
}

static bool thumbnail_buffer_destroy(struct thumbnail_buffer *thumbnail_buffer)
{
    /* don't destroy if still have thumbnails */
    if (!thumbnail_buffer->need_destroy && !wl_list_empty(&thumbnail_buffer->thumbnails)) {
        return false;
    }
    /* mark need_destroy if cannot be destroyed current */
    if (!thumbnail_buffer->can_destroy) {
        thumbnail_buffer->need_destroy = true;
        return false;
    }

    /* force destroy all thumbnails */
    struct thumbnail *thumbnail, *tmp;
    wl_list_for_each_safe(thumbnail, tmp, &thumbnail_buffer->thumbnails, link) {
        thumbnail->buffer = NULL;
        thumbnail_destroy(thumbnail);
    }

    if (thumbnail_buffer->buffer) {
        wlr_buffer_drop(thumbnail_buffer->buffer);
    }

    return true;
}

static void node_thumbnail_destroy(struct thumbnail_buffer *thumbnail_buffer)
{
    if (!thumbnail_buffer_destroy(thumbnail_buffer)) {
        return;
    }

    struct node_thumbnail *node_thumbnail = wl_container_of(thumbnail_buffer, node_thumbnail, base);
    wl_list_remove(&node_thumbnail->source_destroy.link);
    wl_list_remove(&node_thumbnail->source_damage.link);
    wl_list_remove(&node_thumbnail->link);

    free(node_thumbnail);
}

static void view_thumbnail_destroy(struct thumbnail_buffer *thumbnail_buffer)
{
    if (!thumbnail_buffer_destroy(thumbnail_buffer)) {
        return;
    }

    struct view_thumbnail *view_thumbnail = wl_container_of(thumbnail_buffer, view_thumbnail, base);
    wl_list_remove(&view_thumbnail->view_unmap.link);
    wl_list_remove(&view_thumbnail->source_damage.link);
    wl_list_remove(&view_thumbnail->link);

    free(view_thumbnail);
}

static void node_thumbnail_handle_source_destroy(struct wl_listener *listener, void *data)
{
    struct node_thumbnail *node_thumbnail =
        wl_container_of(listener, node_thumbnail, source_destroy);
    /* force destroyed when source node destroy */
    node_thumbnail->base.need_destroy = true;
    node_thumbnail_destroy(&node_thumbnail->base);
}

static void view_thumbnail_handle_source_destroy(struct wl_listener *listener, void *data)
{
    struct view_thumbnail *view_thumbnail = wl_container_of(listener, view_thumbnail, view_unmap);
    /* force destroyed when source node destroy */
    view_thumbnail->base.need_destroy = true;
    view_thumbnail_destroy(&view_thumbnail->base);
}

static void node_thumbnail_handle_source_damage(struct wl_listener *listener, void *data)
{
    struct node_thumbnail *node_thumbnail =
        wl_container_of(listener, node_thumbnail, source_damage);
    node_thumbnail->base.was_damaged = true;
    thumbnail_manager_schedule_frame();
}

static void view_thumbnail_handle_source_damage(struct wl_listener *listener, void *data)
{
    struct view_thumbnail *view_thumbnail =
        wl_container_of(listener, view_thumbnail, source_damage);
    view_thumbnail->base.was_damaged = true;
    thumbnail_manager_schedule_frame();
}

static struct node_thumbnail *node_thumbnail_get_or_create(struct ky_scene_node *node, float scale)
{
    struct node_thumbnail *node_thumbnail = find_node_thumbnail(node, scale);
    if (node_thumbnail) {
        return node_thumbnail;
    }

    node_thumbnail = calloc(1, sizeof(*node_thumbnail));
    if (!node_thumbnail) {
        return NULL;
    }

    thumbnail_buffer_init(&node_thumbnail->base, scale);
    node_thumbnail->base.render = node_thumbnail_render;
    node_thumbnail->base.destroy = node_thumbnail_destroy;

    node_thumbnail->source_node = node;
    node_thumbnail->source_damage.notify = node_thumbnail_handle_source_damage;
    wl_signal_add(&node->events.damage, &node_thumbnail->source_damage);
    node_thumbnail->source_destroy.notify = node_thumbnail_handle_source_destroy;
    wl_signal_add(&node->events.destroy, &node_thumbnail->source_destroy);

    wl_list_insert(&manager->node_thumbnails, &node_thumbnail->link);

    return node_thumbnail;
}

static struct view_thumbnail *view_thumbnail_get_or_create(struct view *view, uint32_t option,
                                                           float scale)
{
    struct view_thumbnail *view_thumbnail = find_view_thumbnail(view, option, scale);
    if (view_thumbnail) {
        return view_thumbnail;
    }

    view_thumbnail = calloc(1, sizeof(*view_thumbnail));
    if (!view_thumbnail) {
        return NULL;
    }

    thumbnail_buffer_init(&view_thumbnail->base, scale);
    view_thumbnail->option = option;
    view_thumbnail->base.render = view_thumbnail_render;
    view_thumbnail->base.destroy = view_thumbnail_destroy;

    view_thumbnail->view = view;
    view_thumbnail->view_unmap.notify = view_thumbnail_handle_source_destroy;
    wl_signal_add(&view->base.events.unmap, &view_thumbnail->view_unmap);

    /* use surface_tree if has no server decoration */
    view_thumbnail->source_node =
        option & THUMBNAIL_DISABLE_DECOR ? &view->surface_tree->node : &view->tree->node;
    view_thumbnail->source_damage.notify = view_thumbnail_handle_source_damage;
    wl_signal_add(&view_thumbnail->source_node->events.damage, &view_thumbnail->source_damage);

    wl_list_insert(&manager->view_thumbnails, &view_thumbnail->link);

    return view_thumbnail;
}

struct thumbnail *thumbnail_create_from_node(struct ky_scene_node *node, float scale)
{
    if (!manager) {
        return NULL;
    }

    struct thumbnail *thumbnail = calloc(1, sizeof(*thumbnail));
    if (!thumbnail) {
        return NULL;
    }

    struct node_thumbnail *node_thumbnail = node_thumbnail_get_or_create(node, scale);
    if (!node_thumbnail) {
        free(thumbnail);
        return NULL;
    }

    thumbnail->buffer = &node_thumbnail->base;
    wl_list_insert(&node_thumbnail->base.thumbnails, &thumbnail->link);
    wl_signal_init(&thumbnail->events.update);
    wl_signal_init(&thumbnail->events.destroy);
    thumbnail->newly_added = thumbnail->wants_update = true;
    /* buffer update is needed */
    thumbnail_manager_schedule_frame();

    return thumbnail;
}

struct thumbnail *thumbnail_create_from_view(struct view *view, uint32_t option, float scale)
{
    if (!manager) {
        return NULL;
    }

    struct thumbnail *thumbnail = calloc(1, sizeof(*thumbnail));
    if (!thumbnail) {
        return NULL;
    }

    struct view_thumbnail *view_thumbnail = view_thumbnail_get_or_create(view, option, scale);
    if (!view_thumbnail) {
        free(thumbnail);
        return NULL;
    }

    thumbnail->buffer = &view_thumbnail->base;
    wl_list_insert(&view_thumbnail->base.thumbnails, &thumbnail->link);
    wl_signal_init(&thumbnail->events.update);
    wl_signal_init(&thumbnail->events.destroy);
    thumbnail->newly_added = thumbnail->wants_update = true;
    /* buffer update is needed */
    thumbnail_manager_schedule_frame();

    return thumbnail;
}

void thumbnail_destroy(struct thumbnail *thumbnail)
{
    if (!thumbnail) {
        return;
    }

    wl_signal_emit_mutable(&thumbnail->events.destroy, NULL);
    wl_list_remove(&thumbnail->link);

    /* thumbnail_buffer may not be destroyed caused by can_destroy == false */
    if (thumbnail->buffer) {
        thumbnail->buffer->destroy(thumbnail->buffer);
    }

    free(thumbnail);
}

void thumbnail_add_update_listener(struct thumbnail *thumbnail, struct wl_listener *listener)
{
    wl_signal_add(&thumbnail->events.update, listener);
}

void thumbnail_add_destroy_listener(struct thumbnail *thumbnail, struct wl_listener *listener)
{
    wl_signal_add(&thumbnail->events.destroy, listener);
}

void thumbnail_mark_wants_update(struct thumbnail *thumbnail, bool wants)
{
    if (thumbnail->wants_update == wants) {
        return;
    }

    thumbnail->wants_update = wants;
    /* should send update if buffer was damaged */
    if (wants) {
        // thumbnail->newly_added = true; // send buffer again
        thumbnail_manager_schedule_frame();
    }
}

static bool thumbnail_buffer_render(struct thumbnail_buffer *thumbnail_buffer)
{
    struct thumbnail *thumbnail, *tmp;

    if (!thumbnail_buffer->was_damaged) {
        /* must have a buffer because was_damaged == false */
        struct thumbnail_update_event event = {
            .buffer = thumbnail_buffer->buffer,
            .buffer_changed = true,
        };
        /* thumbnail_buffer cannot be destroyed in here */
        thumbnail_buffer->can_destroy = false;
        wl_list_for_each_safe(thumbnail, tmp, &thumbnail_buffer->thumbnails, link) {
            if (thumbnail->newly_added) {
                thumbnail->newly_added = false;
                wl_signal_emit_mutable(&thumbnail->events.update, &event);
            }
        }
        /* thumbnail may need be destroyed in update */
        thumbnail_buffer->can_destroy = true;
        thumbnail_buffer->destroy(thumbnail_buffer);
        return true;
    }

    bool has_wants_update = false;
    wl_list_for_each(thumbnail, &thumbnail_buffer->thumbnails, link) {
        has_wants_update |= thumbnail->wants_update;
        if (has_wants_update) {
            break;
        }
    }
    /* skip rendering when no thumbnail want update */
    if (!has_wants_update) {
        return true;
    }

    struct wlr_buffer *buffer = thumbnail_buffer->render(thumbnail_buffer, manager->output);
    /* destroy it when render failed */
    if (!buffer) {
        thumbnail_buffer->need_destroy = true;
        thumbnail_buffer->destroy(thumbnail_buffer);
        return false;
    }

    /* mark buffer is not damaged */
    thumbnail_buffer->was_damaged = false;
    /* drop the prev buffer */
    bool buffer_changed = buffer != thumbnail_buffer->buffer;
    if (buffer_changed) {
        wlr_buffer_drop(thumbnail_buffer->buffer);
        thumbnail_buffer->buffer = buffer;
    }

    struct thumbnail_update_event event = {
        .buffer = buffer,
        .buffer_changed = buffer_changed,
    };
    thumbnail_buffer->can_destroy = false;
    wl_list_for_each_safe(thumbnail, tmp, &thumbnail_buffer->thumbnails, link) {
        if (thumbnail->wants_update) {
            thumbnail->newly_added = false;
            wl_signal_emit_mutable(&thumbnail->events.update, &event);
        }
    }
    thumbnail_buffer->can_destroy = true;
    thumbnail_buffer->destroy(thumbnail_buffer);

    return true;
}

static void thumbnail_manager_handle_output_frame(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->output_frame.link);
    wl_list_init(&manager->output_frame.link);
    manager->frame_pending = false;

    struct node_thumbnail *node_thumbnail, *tmp;
    wl_list_for_each_safe(node_thumbnail, tmp, &manager->node_thumbnails, link) {
        thumbnail_buffer_render(&node_thumbnail->base);
    }

    struct view_thumbnail *view_thumbnail, *view_tmp;
    wl_list_for_each_safe(view_thumbnail, view_tmp, &manager->view_thumbnails, link) {
        thumbnail_buffer_render(&view_thumbnail->base);
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
    wl_list_init(&manager->view_thumbnails);
    manager->output_frame.notify = thumbnail_manager_handle_output_frame;
    manager->output_destroy.notify = thumbnail_manager_handle_output_destroy;

    manager->server = server;
    manager->destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->destroy);

    return true;
}
