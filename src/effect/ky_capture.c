// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include <wlr/types/wlr_buffer.h>

#include <kywc/log.h>
#include <kywc/output.h>
#include <kywc/view.h>

#include "kywc-capture-v1-protocol.h"

#include "effect/capture.h"
#include "effect_p.h"
#include "scene/thumbnail.h"
#include "view/workspace.h"

enum ky_capture_frame_type {
    KY_CAPTURE_FRAME_TYPE_OUTPUT = 0,
    KY_CAPTURE_FRAME_TYPE_WORKSPACE,
    KY_CAPTURE_FRAME_TYPE_TOPLEVEL,
};

struct ky_capture_manager {
    struct wl_global *global;
    struct wl_list frames;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct ky_capture_frame {
    struct wl_resource *resource;
    struct ky_capture_manager *manager;
    struct wl_list link;

    enum ky_capture_frame_type type;
    union {
        struct {
            struct kywc_output *output;
        } output;
        struct {
            struct workspace *workspace;
            struct kywc_output *output;
        } workspace;
        struct {
            struct kywc_view *view;
        } toplevel;
    };

    /* buffer from thumbnail or capture */
    struct wlr_buffer *buffer;
    union {
        struct capture *capture;
        struct thumbnail *thumbnail;
        void *data;
    };
    struct wl_listener buffer_update;
    struct wl_listener buffer_destroy;
};

static void ky_capture_frame_destroy(struct ky_capture_frame *frame)
{
    wl_resource_set_user_data(frame->resource, NULL);

    wl_list_remove(&frame->buffer_update.link);
    wl_list_remove(&frame->buffer_destroy.link);

    if (frame->buffer) {
        wlr_buffer_unlock(frame->buffer);
    }
    if (frame->data) {
        if (frame->type == KY_CAPTURE_FRAME_TYPE_OUTPUT) {
            capture_destroy(frame->capture);
        } else {
            thumbnail_destroy(frame->thumbnail);
        }
    }

    wl_list_remove(&frame->link);
    free(frame);
}

static void frame_handle_release_buffer(struct wl_client *client, struct wl_resource *resource,
                                        uint32_t want_buffer)
{
    struct ky_capture_frame *frame = wl_resource_get_user_data(resource);
    if (!frame) {
        return;
    }

    wlr_buffer_unlock(frame->buffer);
    frame->buffer = NULL;

    if (want_buffer) {
        if (frame->type == KY_CAPTURE_FRAME_TYPE_OUTPUT) {
            capture_mark_wants_update(frame->capture, true, false);
        } else {
            thumbnail_mark_wants_update(frame->thumbnail, true);
        }
    }
}

static void frame_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct kywc_capture_frame_v1_interface frame_impl = {
    .destroy = frame_handle_destroy,
    .release_buffer = frame_handle_release_buffer,
};

static void frame_handle_resource_destroy(struct wl_resource *resource)
{
    struct ky_capture_frame *frame = wl_resource_get_user_data(resource);
    if (frame) {
        ky_capture_frame_destroy(frame);
    }
}

static void frame_handle_buffer_destroy(struct wl_listener *listener, void *data)
{
    struct ky_capture_frame *frame = wl_container_of(listener, frame, buffer_destroy);
    kywc_capture_frame_v1_send_cancelled(frame->resource);
    frame->data = NULL;
    ky_capture_frame_destroy(frame);
}

static void frame_handle_buffer_update(struct wl_listener *listener, void *data)
{
    struct ky_capture_frame *frame = wl_container_of(listener, frame, buffer_update);
    struct wlr_buffer *buffer = NULL;
    uint32_t flags = 0;

    if (frame->type == KY_CAPTURE_FRAME_TYPE_OUTPUT) {
        struct capture_update_event *event = data;
        buffer = event->buffer;
        flags = event->buffer_changed ? 0 : KYWC_CAPTURE_FRAME_V1_FLAGS_REUSED;
    } else {
        struct thumbnail_update_event *event = data;
        buffer = event->buffer;
        flags = event->buffer_changed ? 0 : KYWC_CAPTURE_FRAME_V1_FLAGS_REUSED;
    }

    struct wlr_dmabuf_attributes dmabuf;
    struct wlr_shm_attributes shm;
    uint32_t format, n_planes = 1;
    uint64_t modifier = 0;

    struct {
        int fd;
        uint32_t offset, stride;
    } planes[WLR_DMABUF_MAX_PLANES] = { 0 };

    if (wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
        flags |= KYWC_CAPTURE_FRAME_V1_FLAGS_DMABUF;
        format = dmabuf.format;
        modifier = dmabuf.modifier;
        n_planes = dmabuf.n_planes;
        for (uint32_t i = 0; i < n_planes; i++) {
            planes[i].fd = dmabuf.fd[i];
            planes[i].offset = dmabuf.offset[i];
            planes[i].stride = dmabuf.stride[i];
        }
    } else if (wlr_buffer_get_shm(buffer, &shm)) {
        format = shm.format;
        planes[0].fd = shm.fd;
        planes[0].offset = shm.offset;
        planes[0].stride = shm.stride;
    } else {
        return;
    }

    wlr_buffer_unlock(frame->buffer);
    frame->buffer = wlr_buffer_lock(buffer);

    uint32_t mod_high = modifier >> 32;
    uint32_t mod_low = modifier & 0xFFFFFFFF;

    kywc_capture_frame_v1_send_buffer(frame->resource, planes[0].fd, format, buffer->width,
                                      buffer->height, planes[0].offset, planes[0].stride, mod_high,
                                      mod_low, flags);

    uint32_t version = wl_resource_get_version(frame->resource);
    if (version >= KYWC_CAPTURE_FRAME_V1_BUFFER_DONE_SINCE_VERSION) {
        for (uint32_t i = 1; i < n_planes; i++) {
            kywc_capture_frame_v1_send_buffer_with_plane(frame->resource, i, planes[i].fd,
                                                         planes[i].offset, planes[i].stride);
        }
        kywc_capture_frame_v1_send_buffer_done(frame->resource);
    }

    /* enable update if client wants buffer again in release_buffer */
    if (frame->type == KY_CAPTURE_FRAME_TYPE_OUTPUT) {
        capture_mark_wants_update(frame->capture, false, false);
    } else {
        thumbnail_mark_wants_update(frame->thumbnail, false);
    }
}

static void manager_handle_capture_output(struct wl_client *client, struct wl_resource *resource,
                                          uint32_t id, int32_t overlay_cursor, const char *output)
{
    struct ky_capture_frame *frame = calloc(1, sizeof(*frame));
    if (!frame) {
        wl_client_post_no_memory(client);
        return;
    }

    /* create frame resource with id */
    uint32_t version = wl_resource_get_version(resource);
    frame->resource = wl_resource_create(client, &kywc_capture_frame_v1_interface, version, id);
    if (!frame->resource) {
        wl_client_post_no_memory(client);
        free(frame);
        return;
    }

    wl_resource_set_implementation(frame->resource, &frame_impl, frame,
                                   frame_handle_resource_destroy);

    /* check output is valid */
    struct kywc_output *kywc_output = kywc_output_by_uuid(output);
    if (!kywc_output || !kywc_output->state.enabled) {
        kywc_capture_frame_v1_send_failed(frame->resource);
        wl_resource_set_user_data(frame->resource, NULL);
        free(frame);
        return;
    }

    uint32_t options = overlay_cursor ? CAPTURE_NEED_CURSOR : CAPTURE_NEED_NONE;
    if (version < KYWC_CAPTURE_FRAME_V1_BUFFER_WITH_PLANE_SINCE_VERSION) {
        options |= CAPTURE_NEED_SINGLE_PLANE;
    }
    frame->capture = capture_create_from_output(output_from_kywc_output(kywc_output), options);
    if (!frame->capture) {
        kywc_capture_frame_v1_send_failed(frame->resource);
        wl_resource_set_user_data(frame->resource, NULL);
        free(frame);
        return;
    }

    struct ky_capture_manager *manager = wl_resource_get_user_data(resource);
    frame->manager = manager;
    wl_list_insert(&manager->frames, &frame->link);

    frame->type = KY_CAPTURE_FRAME_TYPE_OUTPUT;
    frame->output.output = kywc_output;

    frame->buffer_update.notify = frame_handle_buffer_update;
    capture_add_update_listener(frame->capture, &frame->buffer_update);
    frame->buffer_destroy.notify = frame_handle_buffer_destroy;
    capture_add_destroy_listener(frame->capture, &frame->buffer_destroy);
}

static void manager_handle_capture_workspace(struct wl_client *client, struct wl_resource *resource,
                                             uint32_t id, const char *workspace, const char *output)
{
    struct ky_capture_frame *frame = calloc(1, sizeof(*frame));
    if (!frame) {
        wl_client_post_no_memory(client);
        return;
    }

    /* create frame resource with id */
    uint32_t version = wl_resource_get_version(resource);
    frame->resource = wl_resource_create(client, &kywc_capture_frame_v1_interface, version, id);
    if (!frame->resource) {
        wl_client_post_no_memory(client);
        free(frame);
        return;
    }

    wl_resource_set_implementation(frame->resource, &frame_impl, frame,
                                   frame_handle_resource_destroy);

    /* check workspace and output */
    struct workspace *ws = workspace_by_uuid(workspace);
    struct kywc_output *kywc_output = kywc_output_by_uuid(output);
    if (!ws || !kywc_output || !kywc_output->state.enabled) {
        kywc_capture_frame_v1_send_failed(frame->resource);
        wl_resource_set_user_data(frame->resource, NULL);
        free(frame);
        return;
    }

    frame->thumbnail = thumbnail_create_from_workspace(
        ws, kywc_output, 1.0, version < KYWC_CAPTURE_FRAME_V1_BUFFER_WITH_PLANE_SINCE_VERSION);
    if (!frame->thumbnail) {
        kywc_capture_frame_v1_send_failed(frame->resource);
        wl_resource_set_user_data(frame->resource, NULL);
        free(frame);
        return;
    }

    struct ky_capture_manager *manager = wl_resource_get_user_data(resource);
    frame->manager = manager;
    wl_list_insert(&manager->frames, &frame->link);

    frame->type = KY_CAPTURE_FRAME_TYPE_WORKSPACE;
    frame->workspace.workspace = ws;
    frame->workspace.output = kywc_output;

    frame->buffer_update.notify = frame_handle_buffer_update;
    thumbnail_add_update_listener(frame->thumbnail, &frame->buffer_update);
    frame->buffer_destroy.notify = frame_handle_buffer_destroy;
    thumbnail_add_destroy_listener(frame->thumbnail, &frame->buffer_destroy);
}

static void manager_handle_capture_toplevel(struct wl_client *client, struct wl_resource *resource,
                                            uint32_t id, const char *toplevel,
                                            uint32_t without_decoration)
{
    struct ky_capture_frame *frame = calloc(1, sizeof(*frame));
    if (!frame) {
        wl_client_post_no_memory(client);
        return;
    }

    /* create frame resource with id */
    uint32_t version = wl_resource_get_version(resource);
    frame->resource = wl_resource_create(client, &kywc_capture_frame_v1_interface, version, id);
    if (!frame->resource) {
        wl_client_post_no_memory(client);
        free(frame);
        return;
    }

    wl_resource_set_implementation(frame->resource, &frame_impl, frame,
                                   frame_handle_resource_destroy);

    /* check toplevel */
    struct kywc_view *kywc_view = kywc_view_by_uuid(toplevel);
    if (!kywc_view || !kywc_view->mapped) {
        kywc_capture_frame_v1_send_failed(frame->resource);
        wl_resource_set_user_data(frame->resource, NULL);
        free(frame);
        return;
    }

    struct view *view = view_from_kywc_view(kywc_view);
    uint32_t options = THUMBNAIL_DISABLE_ROUND_CORNER | THUMBNAIL_ENABLE_SECURITY;
    options |= without_decoration ? THUMBNAIL_DISABLE_DECOR : THUMBNAIL_DISABLE_SHADOW;
    if (version < KYWC_CAPTURE_FRAME_V1_BUFFER_WITH_PLANE_SINCE_VERSION) {
        options |= THUMBNAIL_ENABLE_SINGLE_PLANE;
    }

    frame->thumbnail = thumbnail_create_from_view(view, options, 1.0);
    if (!frame->thumbnail) {
        kywc_capture_frame_v1_send_failed(frame->resource);
        wl_resource_set_user_data(frame->resource, NULL);
        free(frame);
        return;
    }

    struct ky_capture_manager *manager = wl_resource_get_user_data(resource);
    frame->manager = manager;
    wl_list_insert(&manager->frames, &frame->link);

    frame->type = KY_CAPTURE_FRAME_TYPE_TOPLEVEL;
    frame->toplevel.view = kywc_view;

    frame->buffer_update.notify = frame_handle_buffer_update;
    thumbnail_add_update_listener(frame->thumbnail, &frame->buffer_update);
    frame->buffer_destroy.notify = frame_handle_buffer_destroy;
    thumbnail_add_destroy_listener(frame->thumbnail, &frame->buffer_destroy);
}

static void manager_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct kywc_capture_manager_v1_interface ky_capture_manager_impl = {
    .capture_output = manager_handle_capture_output,
    .capture_workspace = manager_handle_capture_workspace,
    .capture_toplevel = manager_handle_capture_toplevel,
    .destroy = manager_handle_destroy,
};

static void ky_capture_manager_bind(struct wl_client *client, void *data, uint32_t version,
                                    uint32_t id)
{
    struct ky_capture_manager *manager = data;
    struct wl_resource *resource =
        wl_resource_create(client, &kywc_capture_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &ky_capture_manager_impl, manager, NULL);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct ky_capture_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct ky_capture_manager *manager = wl_container_of(listener, manager, display_destroy);
    wl_list_remove(&manager->display_destroy.link);
    wl_global_destroy(manager->global);
}

bool ky_capture_manager_create(struct server *server)
{
    struct ky_capture_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    manager->global = wl_global_create(server->display, &kywc_capture_manager_v1_interface, 2,
                                       manager, ky_capture_manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_WARN, "kywc capture manager create failed");
        free(manager);
        return false;
    }

    wl_list_init(&manager->frames);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    return true;
}
