/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <drm_fourcc.h>
#include <wlr/types/wlr_buffer.h>

#include <kywc/log.h>

#include "effect/capture.h"
#include "effect_p.h"
#include "output.h"
#include "scene/thumbnail.h"
#include "server.h"
#include "view/view.h"
#include "ext-foreign-toplevel-list-v1-protocol.h"
#include "ext-image-capture-source-v1-protocol.h"
#include "ext-image-copy-capture-v1-protocol.h"

#define EXT_CAPTURE_VERSION 1

enum ext_source_kind {
    EXT_SOURCE_OUTPUT,
    EXT_SOURCE_TOPLEVEL,
};

struct ext_capture_manager;
struct ext_capture_source;
struct ext_capture_session;

struct ext_capture_frame {
    struct wl_resource *resource;
    struct ext_capture_session *session;
    struct wlr_buffer *buffer;
    pixman_region32_t buffer_damage;
    bool capturing;
};

struct ext_capture_session {
    struct wl_resource *resource;
    struct wl_list link;
    struct ext_capture_source *source;
    struct ext_capture_frame *frame;
    pixman_region32_t damage;
    uint32_t options;
    bool stopped;
};

struct ext_capture_source {
    struct ext_capture_manager *manager;
    struct wl_resource *resource;
    struct wl_list sessions;
    enum ext_source_kind kind;
    union {
        struct output *output;
        struct view *view;
    } target;

    union {
        struct capture *capture;
        struct thumbnail *thumbnail;
        void *ptr;
    } backend;
    struct wlr_buffer *buffer;
    uint32_t width, height;
    bool stopped;
    bool stopping;

    struct wl_listener backend_update;
    struct wl_listener backend_destroy;
};

struct ext_toplevel {
    struct wl_list link;
    struct ext_capture_manager *manager;
    struct view *view;
    struct wl_list resources;
    char identifier[40];
    bool closed;

    struct wl_listener unmap;
    struct wl_listener title;
    struct wl_listener app_id;
};

struct ext_capture_manager {
    struct server *server;
    struct wl_global *output_source_global;
    struct wl_global *toplevel_source_global;
    struct wl_global *copy_capture_global;
    struct wl_global *toplevel_list_global;
    struct wl_list toplevels;
    struct wl_list toplevel_list_resources;
    uint32_t next_identifier;

    struct wl_listener new_mapped_view;
    struct wl_listener display_destroy;
};

struct ext_cursor_session {
    struct wl_resource *resource;
    bool capture_session_created;
};

static const struct ext_image_capture_source_v1_interface source_impl;
static const struct ext_foreign_toplevel_handle_v1_interface toplevel_handle_impl;
static void session_maybe_destroy(struct ext_capture_session *session);
static void source_maybe_destroy(struct ext_capture_source *source);

static void frame_finish(struct ext_capture_frame *frame)
{
    if (!frame) {
        return;
    }

    struct ext_capture_session *session = frame->session;
    if (session && session->frame == frame) {
        session->frame = NULL;
    }

    if (frame->resource) {
        wl_resource_set_user_data(frame->resource, NULL);
    }

    wlr_buffer_unlock(frame->buffer);
    pixman_region32_fini(&frame->buffer_damage);
    free(frame);
    if (session) {
        session_maybe_destroy(session);
    }
}

static void frame_fail(struct ext_capture_frame *frame,
                       enum ext_image_copy_capture_frame_v1_failure_reason reason)
{
    ext_image_copy_capture_frame_v1_send_failed(frame->resource, reason);
    frame_finish(frame);
}

static bool frame_copy(struct ext_capture_frame *frame)
{
    struct ext_capture_source *source = frame->session->source;
    if (!source->buffer || frame->buffer->width != (int)source->width ||
        frame->buffer->height != (int)source->height) {
        frame_fail(frame, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS);
        return false;
    }

    void *data = NULL;
    uint32_t format = DRM_FORMAT_INVALID;
    size_t stride = 0;
    if (!wlr_buffer_begin_data_ptr_access(frame->buffer, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &data,
            &format, &stride)) {
        frame_fail(frame, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS);
        return false;
    }

    struct wlr_box box = { .width = source->width, .height = source->height };
    capture_read_buffer(source->buffer, format, stride, &box, data);
    wlr_buffer_end_data_ptr_access(frame->buffer);

    int rect_count = 0;
    const pixman_box32_t *rects =
        pixman_region32_rectangles(&frame->session->damage, &rect_count);
    for (int i = 0; i < rect_count; i++) {
        ext_image_copy_capture_frame_v1_send_damage(
            frame->resource, rects[i].x1, rects[i].y1, rects[i].x2 - rects[i].x1,
            rects[i].y2 - rects[i].y1);
    }
    pixman_region32_clear(&frame->session->damage);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t seconds = now.tv_sec;
    ext_image_copy_capture_frame_v1_send_transform(frame->resource, WL_OUTPUT_TRANSFORM_NORMAL);
    ext_image_copy_capture_frame_v1_send_presentation_time(frame->resource, seconds >> 32,
        seconds, now.tv_nsec);
    ext_image_copy_capture_frame_v1_send_ready(frame->resource);
    frame_finish(frame);
    return true;
}

static void session_send_constraints(struct ext_capture_session *session)
{
    struct ext_capture_source *source = session->source;
    ext_image_copy_capture_session_v1_send_buffer_size(session->resource, source->width,
        source->height);
    ext_image_copy_capture_session_v1_send_shm_format(session->resource,
        WL_SHM_FORMAT_ARGB8888);
    ext_image_copy_capture_session_v1_send_done(session->resource);
}

static void session_stop(struct ext_capture_session *session)
{
    if (session->stopped) {
        return;
    }
    session->stopped = true;
    struct wl_resource *resource = session->resource;
    if (session->frame) {
        frame_fail(session->frame, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED);
    }
    if (resource) {
        ext_image_copy_capture_session_v1_send_stopped(resource);
    }
}

static void source_stop(struct ext_capture_source *source)
{
    if (source->stopped) {
        return;
    }
    source->stopped = true;
    source->stopping = true;
    struct ext_capture_session *session, *tmp;
    wl_list_for_each_safe(session, tmp, &source->sessions, link) {
        session_stop(session);
    }
    source->stopping = false;
    source_maybe_destroy(source);
}

static void source_maybe_destroy(struct ext_capture_source *source)
{
    if (source->stopping || source->resource || !wl_list_empty(&source->sessions)) {
        return;
    }

    if (source->backend.ptr) {
        wl_list_remove(&source->backend_update.link);
        wl_list_remove(&source->backend_destroy.link);
        if (source->kind == EXT_SOURCE_OUTPUT) {
            capture_destroy(source->backend.capture);
        } else {
            thumbnail_destroy(source->backend.thumbnail);
        }
    }
    wlr_buffer_unlock(source->buffer);
    free(source);
}

static void source_handle_backend_destroy(struct wl_listener *listener, void *data)
{
    struct ext_capture_source *source = wl_container_of(listener, source, backend_destroy);
    source->backend.ptr = NULL;
    wl_list_remove(&source->backend_update.link);
    wl_list_remove(&source->backend_destroy.link);
    source_stop(source);
}

static void source_handle_backend_update(struct wl_listener *listener, void *data)
{
    struct ext_capture_source *source = wl_container_of(listener, source, backend_update);
    struct wlr_buffer *buffer;
    if (source->kind == EXT_SOURCE_OUTPUT) {
        struct capture_update_event *event = data;
        buffer = event->buffer;
    } else {
        struct thumbnail_update_event *event = data;
        buffer = event->buffer;
    }

    bool resized = source->width != (uint32_t)buffer->width ||
                   source->height != (uint32_t)buffer->height;
    source->width = buffer->width;
    source->height = buffer->height;
    if (source->buffer != buffer) {
        wlr_buffer_unlock(source->buffer);
        source->buffer = wlr_buffer_lock(buffer);
    }

    struct ext_capture_session *session, *tmp;
    wl_list_for_each_safe(session, tmp, &source->sessions, link) {
        if (session->stopped) {
            continue;
        }
        if (resized) {
            session_send_constraints(session);
        }
        pixman_region32_union_rect(&session->damage, &session->damage, 0, 0, source->width,
            source->height);
        if (session->frame && session->frame->capturing) {
            frame_copy(session->frame);
        }
    }
}

static bool source_create_backend(struct ext_capture_source *source, uint32_t options)
{
    if (source->backend.ptr) {
        return true;
    }

    if (source->kind == EXT_SOURCE_OUTPUT) {
        uint32_t capture_options = options & EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS
            ? CAPTURE_NEED_CURSOR : CAPTURE_NEED_NONE;
        source->backend.capture =
            capture_create_from_output(source->target.output, capture_options);
        if (!source->backend.capture) {
            return false;
        }
        source->backend_update.notify = source_handle_backend_update;
        capture_add_update_listener(source->backend.capture, &source->backend_update);
        source->backend_destroy.notify = source_handle_backend_destroy;
        capture_add_destroy_listener(source->backend.capture, &source->backend_destroy);
    } else {
        source->backend.thumbnail = thumbnail_create_from_view(
            source->target.view, THUMBNAIL_DISABLE_SHADOW | THUMBNAIL_ENABLE_SECURITY, 1.0f);
        if (!source->backend.thumbnail) {
            return false;
        }
        source->backend_update.notify = source_handle_backend_update;
        thumbnail_add_update_listener(source->backend.thumbnail, &source->backend_update);
        source->backend_destroy.notify = source_handle_backend_destroy;
        thumbnail_add_destroy_listener(source->backend.thumbnail, &source->backend_destroy);
        thumbnail_update(source->backend.thumbnail);
    }
    return !source->stopped;
}

static void source_handle_resource_destroy(struct wl_resource *resource)
{
    struct ext_capture_source *source = wl_resource_get_user_data(resource);
    if (!source) {
        return;
    }
    source->resource = NULL;
    source_maybe_destroy(source);
}

static void source_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct ext_image_capture_source_v1_interface source_impl = {
    .destroy = source_handle_destroy,
};

static struct ext_capture_source *source_create(struct ext_capture_manager *manager,
    struct wl_client *client, uint32_t id,
    enum ext_source_kind kind, void *target)
{
    struct wl_resource *resource = wl_resource_create(
        client, &ext_image_capture_source_v1_interface, EXT_CAPTURE_VERSION, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return NULL;
    }

    struct ext_capture_source *source = calloc(1, sizeof(*source));
    if (!source) {
        wl_resource_post_no_memory(resource);
        wl_resource_destroy(resource);
        return NULL;
    }
    source->manager = manager;
    source->resource = resource;
    source->kind = kind;
    source->target.output = target;
    wl_list_init(&source->sessions);
    wl_resource_set_implementation(resource, &source_impl, source,
        source_handle_resource_destroy);

    if (!target) {
        source->stopped = true;
    } else if (kind == EXT_SOURCE_OUTPUT) {
        struct output *output = target;
        source->width = output->geometry.width;
        source->height = output->geometry.height;
    } else {
        struct view *view = target;
        source->width = view->base.geometry.width;
        source->height = view->base.geometry.height;
    }
    return source;
}

static void frame_handle_resource_destroy(struct wl_resource *resource)
{
    frame_finish(wl_resource_get_user_data(resource));
}

static void frame_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void frame_handle_attach_buffer(struct wl_client *client, struct wl_resource *resource,
        struct wl_resource *buffer_resource)
{
    struct ext_capture_frame *frame = wl_resource_get_user_data(resource);
    if (!frame) {
        return;
    }
    if (frame->capturing) {
        wl_resource_post_error(resource, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_ALREADY_CAPTURED,
                               "attach_buffer sent after capture");
        return;
    }
    struct wlr_buffer *buffer = wlr_buffer_try_from_resource(buffer_resource);
    if (!buffer) {
        wl_resource_post_no_memory(resource);
        return;
    }
    wlr_buffer_unlock(frame->buffer);
    frame->buffer = buffer;
}

static void frame_handle_damage_buffer(struct wl_client *client, struct wl_resource *resource,
        int32_t x, int32_t y, int32_t width, int32_t height)
{
    struct ext_capture_frame *frame = wl_resource_get_user_data(resource);
    if (!frame) {
        return;
    }
    if (frame->capturing) {
        wl_resource_post_error(resource, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_ALREADY_CAPTURED,
                               "damage_buffer sent after capture");
        return;
    }
    if (x < 0 || y < 0 || width <= 0 || height <= 0) {
        wl_resource_post_error(resource,
                               EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_INVALID_BUFFER_DAMAGE,
                               "invalid buffer damage");
        return;
    }
    pixman_region32_union_rect(&frame->buffer_damage, &frame->buffer_damage, x, y, width, height);
}

static void frame_handle_capture(struct wl_client *client, struct wl_resource *resource)
{
    struct ext_capture_frame *frame = wl_resource_get_user_data(resource);
    if (!frame) {
        return;
    }
    if (frame->capturing) {
        wl_resource_post_error(resource, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_ALREADY_CAPTURED,
            "capture sent twice");
        return;
    }
    if (!frame->buffer) {
        wl_resource_post_error(resource, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_NO_BUFFER,
    "capture sent without a buffer");
        return;
    }
    frame->capturing = true;
    if (frame->session->stopped) {
        frame_fail(frame, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED);
    } else if (frame->session->source->buffer &&
               !pixman_region32_empty(&frame->session->damage)) {
        frame_copy(frame);
    }
}

static const struct ext_image_copy_capture_frame_v1_interface frame_impl = {
    .destroy = frame_handle_destroy,
    .attach_buffer = frame_handle_attach_buffer,
    .damage_buffer = frame_handle_damage_buffer,
    .capture = frame_handle_capture,
};

static void session_maybe_destroy(struct ext_capture_session *session)
{
    if (session->resource || session->frame) {
        return;
    }
    struct ext_capture_source *source = session->source;
    wl_list_remove(&session->link);
    pixman_region32_fini(&session->damage);
    free(session);
    source_maybe_destroy(source);
}

static void session_handle_resource_destroy(struct wl_resource *resource)
{
    struct ext_capture_session *session = wl_resource_get_user_data(resource);
    if (!session) {
        return;
    }
    session->resource = NULL;
    session_maybe_destroy(session);
}

static void session_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void session_handle_create_frame(struct wl_client *client, struct wl_resource *resource,
        uint32_t id)
{
    struct ext_capture_session *session = wl_resource_get_user_data(resource);
    if (!session) {
        return;
    }
    if (session->frame) {
        wl_resource_post_error(resource,
                               EXT_IMAGE_COPY_CAPTURE_SESSION_V1_ERROR_DUPLICATE_FRAME,
                               "create_frame sent before destroying previous frame");
        return;
    }

    struct wl_resource *frame_resource = wl_resource_create(
        client, &ext_image_copy_capture_frame_v1_interface, EXT_CAPTURE_VERSION, id);
    if (!frame_resource) {
        wl_resource_post_no_memory(resource);
        return;
    }
    struct ext_capture_frame *frame = calloc(1, sizeof(*frame));
    if (!frame) {
        wl_resource_post_no_memory(resource);
        wl_resource_destroy(frame_resource);
        return;
    }
    frame->resource = frame_resource;
    frame->session = session;
    pixman_region32_init(&frame->buffer_damage);
    session->frame = frame;
    wl_resource_set_implementation(frame_resource, &frame_impl, frame,
                                   frame_handle_resource_destroy);
}

static const struct ext_image_copy_capture_session_v1_interface session_impl = {
    .create_frame = session_handle_create_frame,
    .destroy = session_handle_destroy,
};

static struct ext_capture_session *session_create(struct ext_capture_source *source,
        struct wl_client *client, uint32_t id, uint32_t options)
{
    struct wl_resource *resource = wl_resource_create(
        client, &ext_image_copy_capture_session_v1_interface, EXT_CAPTURE_VERSION, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return NULL;
    }
    if (!source || source->stopped || !source_create_backend(source, options)) {
        ext_image_copy_capture_session_v1_send_stopped(resource);
        wl_resource_set_implementation(resource, &session_impl, NULL, NULL);
        return NULL;
    }

    struct ext_capture_session *session = calloc(1, sizeof(*session));
    if (!session) {
        wl_resource_post_no_memory(resource);
        return NULL;
    }
    session->resource = resource;
    session->source = source;
    session->options = options;
    pixman_region32_init_rect(&session->damage, 0, 0, source->width, source->height);
    wl_list_insert(&source->sessions, &session->link);
    wl_resource_set_implementation(resource, &session_impl, session,
                                   session_handle_resource_destroy);
    session_send_constraints(session);
    return session;
}

static void cursor_session_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void cursor_session_handle_get_capture_session(struct wl_client *client,
                                                      struct wl_resource *resource, uint32_t id)
{
    struct ext_cursor_session *cursor = wl_resource_get_user_data(resource);
    if (cursor->capture_session_created) {
        wl_resource_post_error(
            resource, EXT_IMAGE_COPY_CAPTURE_CURSOR_SESSION_V1_ERROR_DUPLICATE_SESSION,
            "get_capture_session sent twice");
        return;
    }
    cursor->capture_session_created = true;
    struct wl_resource *session = wl_resource_create(
        client, &ext_image_copy_capture_session_v1_interface, EXT_CAPTURE_VERSION, id);
    if (!session) {
        wl_resource_post_no_memory(resource);
        return;
    }
    wl_resource_set_implementation(session, &session_impl, NULL, NULL);
    ext_image_copy_capture_session_v1_send_stopped(session);
}

static const struct ext_image_copy_capture_cursor_session_v1_interface cursor_session_impl = {
    .destroy = cursor_session_handle_destroy,
    .get_capture_session = cursor_session_handle_get_capture_session,
};

static void cursor_session_resource_destroy(struct wl_resource *resource)
{
    free(wl_resource_get_user_data(resource));
}

static void copy_manager_handle_create_session(struct wl_client *client,
        struct wl_resource *resource, uint32_t id,
        struct wl_resource *source_resource,
        uint32_t options)
{
    if (options & ~EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS) {
        wl_resource_post_error(resource, EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_ERROR_INVALID_OPTION,
                               "invalid capture option 0x%x", options);
        return;
    }
    struct ext_capture_source *source = NULL;
    if (source_resource &&
        wl_resource_instance_of(source_resource, &ext_image_capture_source_v1_interface,
                                &source_impl)) {
        source = wl_resource_get_user_data(source_resource);
    }
    session_create(source, client, id, options);
}

static void copy_manager_handle_create_cursor_session(struct wl_client *client,
                                                      struct wl_resource *resource, uint32_t id,
                                                      struct wl_resource *source,
                                                      struct wl_resource *pointer)
{
    struct wl_resource *cursor_resource = wl_resource_create(
        client, &ext_image_copy_capture_cursor_session_v1_interface, EXT_CAPTURE_VERSION, id);
    if (!cursor_resource) {
        wl_resource_post_no_memory(resource);
        return;
    }
    struct ext_cursor_session *cursor = calloc(1, sizeof(*cursor));
    if (!cursor) {
        wl_resource_post_no_memory(resource);
        wl_resource_destroy(cursor_resource);
        return;
    }
    cursor->resource = cursor_resource;
    wl_resource_set_implementation(cursor_resource, &cursor_session_impl, cursor,
                                   cursor_session_resource_destroy);
}

static void manager_resource_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct ext_image_copy_capture_manager_v1_interface copy_manager_impl = {
    .create_session = copy_manager_handle_create_session,
    .create_pointer_cursor_session = copy_manager_handle_create_cursor_session,
    .destroy = manager_resource_destroy,
};

static void copy_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *resource = wl_resource_create(
        client, &ext_image_copy_capture_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &copy_manager_impl, data, NULL);
}

static void output_source_handle_create(struct wl_client *client, struct wl_resource *resource,
                                        uint32_t id, struct wl_resource *output_resource)
{
    struct ext_capture_manager *manager = wl_resource_get_user_data(resource);
    struct output *output = output_from_resource(output_resource);
    source_create(manager, client, id, EXT_SOURCE_OUTPUT, output);
}

static const struct ext_output_image_capture_source_manager_v1_interface output_source_impl = {
    .create_source = output_source_handle_create,
    .destroy = manager_resource_destroy,
};

static void output_source_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *resource = wl_resource_create(
        client, &ext_output_image_capture_source_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &output_source_impl, data, NULL);
}

static void toplevel_handle_resource_destroy(struct wl_resource *resource)
{
    if (wl_resource_get_user_data(resource)) {
        wl_list_remove(wl_resource_get_link(resource));
    }
}

static void toplevel_handle_destroy_request(struct wl_client *client,
                                            struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct ext_foreign_toplevel_handle_v1_interface toplevel_handle_impl = {
    .destroy = toplevel_handle_destroy_request,
};

static void toplevel_send_details(struct ext_toplevel *toplevel, struct wl_resource *resource)
{
    if (toplevel->view->base.title) {
        ext_foreign_toplevel_handle_v1_send_title(resource, toplevel->view->base.title);
    }
    if (toplevel->view->base.app_id) {
        ext_foreign_toplevel_handle_v1_send_app_id(resource, toplevel->view->base.app_id);
    }
    ext_foreign_toplevel_handle_v1_send_identifier(resource, toplevel->identifier);
    ext_foreign_toplevel_handle_v1_send_done(resource);
}

static void toplevel_create_resource(struct ext_toplevel *toplevel,
                                     struct wl_resource *list_resource)
{
    struct wl_client *client = wl_resource_get_client(list_resource);
    struct wl_resource *resource = wl_resource_create(
        client, &ext_foreign_toplevel_handle_v1_interface,
        wl_resource_get_version(list_resource), 0);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &toplevel_handle_impl, toplevel,
                                   toplevel_handle_resource_destroy);
    wl_list_insert(&toplevel->resources, wl_resource_get_link(resource));
    ext_foreign_toplevel_list_v1_send_toplevel(list_resource, resource);
    toplevel_send_details(toplevel, resource);
}

static void toplevel_handle_unmap(struct wl_listener *listener, void *data)
{
    struct ext_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
    toplevel->closed = true;
    wl_list_remove(&toplevel->unmap.link);
    wl_list_remove(&toplevel->title.link);
    wl_list_remove(&toplevel->app_id.link);
    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        ext_foreign_toplevel_handle_v1_send_closed(resource);
    }
}

static void toplevel_handle_title(struct wl_listener *listener, void *data)
{
    struct ext_toplevel *toplevel = wl_container_of(listener, toplevel, title);
    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        ext_foreign_toplevel_handle_v1_send_title(
            resource, toplevel->view->base.title ? toplevel->view->base.title : "");
        ext_foreign_toplevel_handle_v1_send_done(resource);
    }
}

static void toplevel_handle_app_id(struct wl_listener *listener, void *data)
{
    struct ext_toplevel *toplevel = wl_container_of(listener, toplevel, app_id);
    struct wl_resource *resource;
    wl_resource_for_each(resource, &toplevel->resources) {
        ext_foreign_toplevel_handle_v1_send_app_id(
            resource, toplevel->view->base.app_id ? toplevel->view->base.app_id : "");
        ext_foreign_toplevel_handle_v1_send_done(resource);
    }
}

static void manager_handle_new_mapped_view(struct wl_listener *listener, void *data)
{
    struct ext_capture_manager *manager =
        wl_container_of(listener, manager, new_mapped_view);
    struct ext_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    if (!toplevel) {
        return;
    }
    toplevel->manager = manager;
    toplevel->view = view_from_kywc_view(data);
    wl_list_init(&toplevel->resources);
    snprintf(toplevel->identifier, sizeof(toplevel->identifier), "gxde-wlcom-%u",
             ++manager->next_identifier);
    wl_list_insert(&manager->toplevels, &toplevel->link);

    toplevel->unmap.notify = toplevel_handle_unmap;
    wl_signal_add(&toplevel->view->base.events.unmap, &toplevel->unmap);
    toplevel->title.notify = toplevel_handle_title;
    wl_signal_add(&toplevel->view->base.events.title, &toplevel->title);
    toplevel->app_id.notify = toplevel_handle_app_id;
    wl_signal_add(&toplevel->view->base.events.app_id, &toplevel->app_id);

    struct wl_resource *list_resource;
    wl_resource_for_each(list_resource, &manager->toplevel_list_resources) {
        toplevel_create_resource(toplevel, list_resource);
    }
}

static void toplevel_list_resource_destroy(struct wl_resource *resource)
{
    if (wl_resource_get_user_data(resource)) {
        wl_list_remove(wl_resource_get_link(resource));
    }
}

static void toplevel_list_handle_stop(struct wl_client *client, struct wl_resource *resource)
{
    ext_foreign_toplevel_list_v1_send_finished(resource);
    wl_list_remove(wl_resource_get_link(resource));
    wl_list_init(wl_resource_get_link(resource));
}

static const struct ext_foreign_toplevel_list_v1_interface toplevel_list_impl = {
    .stop = toplevel_list_handle_stop,
    .destroy = manager_resource_destroy,
};

static void toplevel_list_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct ext_capture_manager *manager = data;
    struct wl_resource *resource =
        wl_resource_create(client, &ext_foreign_toplevel_list_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &toplevel_list_impl, manager,
                                   toplevel_list_resource_destroy);
    wl_list_insert(&manager->toplevel_list_resources, wl_resource_get_link(resource));
    struct ext_toplevel *toplevel;
    wl_list_for_each(toplevel, &manager->toplevels, link) {
        if (!toplevel->closed) {
            toplevel_create_resource(toplevel, resource);
        }
    }
}

static void toplevel_source_handle_create(struct wl_client *client, struct wl_resource *resource,
                                          uint32_t id, struct wl_resource *handle_resource)
{
    struct ext_capture_manager *manager = wl_resource_get_user_data(resource);
    struct ext_toplevel *toplevel = NULL;
    if (handle_resource &&
        wl_resource_instance_of(handle_resource, &ext_foreign_toplevel_handle_v1_interface,
                                &toplevel_handle_impl)) {
        toplevel = wl_resource_get_user_data(handle_resource);
    }
    source_create(manager, client, id, EXT_SOURCE_TOPLEVEL,
                  toplevel && !toplevel->closed ? toplevel->view : NULL);
}

static const struct ext_foreign_toplevel_image_capture_source_manager_v1_interface
    toplevel_source_impl = {
        .create_source = toplevel_source_handle_create,
        .destroy = manager_resource_destroy,
    };

static void toplevel_source_bind(struct wl_client *client, void *data, uint32_t version,
                                 uint32_t id)
{
    struct wl_resource *resource = wl_resource_create(
        client, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &toplevel_source_impl, data, NULL);
}

static void manager_handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct ext_capture_manager *manager =
        wl_container_of(listener, manager, display_destroy);
    wl_list_remove(&manager->new_mapped_view.link);
    wl_list_remove(&manager->display_destroy.link);

    struct wl_resource *list_resource, *list_resource_tmp;
    wl_resource_for_each_safe(list_resource, list_resource_tmp,
                              &manager->toplevel_list_resources) {
        wl_resource_set_user_data(list_resource, NULL);
        wl_list_remove(wl_resource_get_link(list_resource));
    }

    struct ext_toplevel *toplevel, *tmp;
    wl_list_for_each_safe(toplevel, tmp, &manager->toplevels, link) {
        struct wl_resource *resource, *resource_tmp;
        wl_resource_for_each_safe(resource, resource_tmp, &toplevel->resources) {
            wl_resource_set_user_data(resource, NULL);
            wl_list_remove(wl_resource_get_link(resource));
        }
        if (!toplevel->closed) {
            wl_list_remove(&toplevel->unmap.link);
            wl_list_remove(&toplevel->title.link);
            wl_list_remove(&toplevel->app_id.link);
        }
        wl_list_remove(&toplevel->link);
        free(toplevel);
    }
    wl_global_destroy(manager->output_source_global);
    wl_global_destroy(manager->toplevel_source_global);
    wl_global_destroy(manager->copy_capture_global);
    wl_global_destroy(manager->toplevel_list_global);
    free(manager);
}

bool ext_image_copy_capture_manager_create(struct server *server)
{
    struct ext_capture_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }
    manager->server = server;
    wl_list_init(&manager->toplevels);
    wl_list_init(&manager->toplevel_list_resources);

    manager->output_source_global = wl_global_create(
        server->display, &ext_output_image_capture_source_manager_v1_interface,
        EXT_CAPTURE_VERSION, manager, output_source_bind);
    manager->toplevel_source_global = wl_global_create(
        server->display, &ext_foreign_toplevel_image_capture_source_manager_v1_interface,
        EXT_CAPTURE_VERSION, manager, toplevel_source_bind);
    manager->copy_capture_global = wl_global_create(
        server->display, &ext_image_copy_capture_manager_v1_interface, EXT_CAPTURE_VERSION,
        manager, copy_manager_bind);
    manager->toplevel_list_global = wl_global_create(
        server->display, &ext_foreign_toplevel_list_v1_interface, EXT_CAPTURE_VERSION, manager,
        toplevel_list_bind);
    if (!manager->output_source_global || !manager->toplevel_source_global ||
        !manager->copy_capture_global || !manager->toplevel_list_global) {
        kywc_log(KYWC_ERROR, "failed to create ext image copy capture globals");
        if (manager->output_source_global) {
            wl_global_destroy(manager->output_source_global);
        }
        if (manager->toplevel_source_global) {
            wl_global_destroy(manager->toplevel_source_global);
        }
        if (manager->copy_capture_global) {
            wl_global_destroy(manager->copy_capture_global);
        }
        if (manager->toplevel_list_global) {
            wl_global_destroy(manager->toplevel_list_global);
        }
        free(manager);
        return false;
    }

    manager->new_mapped_view.notify = manager_handle_new_mapped_view;
    kywc_view_add_new_mapped_listener(&manager->new_mapped_view);
    manager->display_destroy.notify = manager_handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    kywc_log(KYWC_INFO, "enabled ext image copy capture protocols");
    return true;
}
