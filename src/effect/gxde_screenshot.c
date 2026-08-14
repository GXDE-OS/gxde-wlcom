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
 * ----------------------------------------------------------------------------
 * GXDE's own screenshot manager protocol implementation.
 * Not so original, it is actually based on Kylin's screenshot manager protocol
 */

#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <drm_fourcc.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/box.h>

#include <kywc/log.h>
#include <kywc/output.h>
#include <kywc/view.h>

#include "gxde-screenshot-v1-protocol.h"

#include "effect/capture.h"
#include "effect_p.h"
#include "output.h"
#include "render/renderer.h"
#include "scene/thumbnail.h"
#include "view/view.h"

#define GXDE_SCREENSHOT_MANAGER_VERSION 1

enum gxde_screenshot_source_type {
    GXDE_SCREENSHOT_SOURCE_CAPTURE = 0,
    GXDE_SCREENSHOT_SOURCE_THUMBNAIL,
};

struct gxde_screenshot_manager {
    struct server* server;
    struct wl_global* global;
    struct wl_list resources;
    struct wl_list frames;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct gxde_screenshot_manager_resource {
    struct wl_resource* resource;
    struct gxde_screenshot_manager* manager;
    struct wl_list link;
};

struct gxde_screenshot_frame {
    struct wl_resource* resource;
    struct gxde_screenshot_manager* manager;
    struct wl_list link;

    enum gxde_screenshot_source_type source_type;
    struct wlr_buffer* buffer;
    /* thumbnail 的 CPU 可读拷贝(shm)，客户端 mmap 即可读取，见 frame_readback_to_shm */
    struct wlr_buffer* cpu_buffer;
    union {
        struct capture* capture;
        struct thumbnail* thumbnail;
        void* source;
    };

    struct wl_listener buffer_update;
    struct wl_listener buffer_destroy;
};

struct gxde_screenshot_buffer_plane {
    int fd;
    uint32_t offset;
    uint32_t stride;
};

static void frame_detach_listeners(struct gxde_screenshot_frame* frame) {
    if (!wl_list_empty(&frame->buffer_update.link)) {
        wl_list_remove(&frame->buffer_update.link);
        wl_list_init(&frame->buffer_update.link);
    }

    if (!wl_list_empty(&frame->buffer_destroy.link)) {
        wl_list_remove(&frame->buffer_destroy.link);
        wl_list_init(&frame->buffer_destroy.link);
    }
}

static void gxde_screenshot_frame_destroy(struct gxde_screenshot_frame* frame) {
    wl_resource_set_user_data(frame->resource, NULL);
    frame_detach_listeners(frame);

    if (frame->buffer) {
        wlr_buffer_unlock(frame->buffer);
        frame->buffer = NULL;
    }

    if (frame->cpu_buffer) {
        wlr_buffer_drop(frame->cpu_buffer);
        frame->cpu_buffer = NULL;
    }

    if (frame->source) {
        if (frame->source_type == GXDE_SCREENSHOT_SOURCE_CAPTURE) {
            capture_destroy(frame->capture);
        } else {
            thumbnail_destroy(frame->thumbnail);
        }

        frame->source = NULL;
    }

    if (!wl_list_empty(&frame->link)) {
        wl_list_remove(&frame->link);
    }
    free(frame);
}

static void gxde_screenshot_manager_destroy_frames(struct gxde_screenshot_manager* manager) {
    struct gxde_screenshot_frame* frame;
    struct gxde_screenshot_frame* tmp;
    wl_list_for_each_safe(frame, tmp, &manager->frames, link) {
        gxde_screenshot_frame_destroy(frame);
    }
}

static struct gxde_screenshot_manager* manager_from_resource(struct wl_resource* resource) {
    struct gxde_screenshot_manager_resource* manager_resource =
        wl_resource_get_user_data(resource);
    return manager_resource ? manager_resource->manager : NULL;
}

static void gxde_screenshot_manager_destroy_resources(struct gxde_screenshot_manager* manager) {
    struct gxde_screenshot_manager_resource* manager_resource;
    struct gxde_screenshot_manager_resource* tmp;

    wl_list_for_each_safe(manager_resource, tmp, &manager->resources, link) {
        wl_resource_set_user_data(manager_resource->resource, NULL);
        wl_list_remove(&manager_resource->link);
        free(manager_resource);
    }
}

static void frame_handle_destroy(struct wl_client* client, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void frame_handle_release_buffer(struct wl_client* client, struct wl_resource* resource,
        uint32_t want_buffer) {
    struct gxde_screenshot_frame* frame = wl_resource_get_user_data(resource);
    if (!frame) {
        return;
    }

    if (frame->buffer) {
        wlr_buffer_unlock(frame->buffer);
        frame->buffer = NULL;
    }

    if (want_buffer && frame->source) {
        if (frame->source_type == GXDE_SCREENSHOT_SOURCE_CAPTURE) {
            capture_mark_wants_update(frame->capture, true, false);
        } else {
            thumbnail_mark_wants_update(frame->thumbnail, true);
        }
    }
}

static const struct gxde_screenshot_frame_v1_interface frame_impl = {
    .destroy = frame_handle_destroy,
    .release_buffer = frame_handle_release_buffer,
};

static void frame_handle_resource_destroy(struct wl_resource* resource) {
    struct gxde_screenshot_frame* frame = wl_resource_get_user_data(resource);
    if (frame) {
        gxde_screenshot_frame_destroy(frame);
    }
}

static struct gxde_screenshot_frame* frame_create(struct wl_client* client,
        struct wl_resource* manager_resource, uint32_t id) {
    struct gxde_screenshot_frame* frame = calloc(1, sizeof(*frame));
    if (!frame) {
        wl_client_post_no_memory(client);
        return NULL;
    }

    wl_list_init(&frame->link);
    wl_list_init(&frame->buffer_update.link);
    wl_list_init(&frame->buffer_destroy.link);

    frame->resource = wl_resource_create(client, &gxde_screenshot_frame_v1_interface,
        wl_resource_get_version(manager_resource), id);
    if (!frame->resource) {
        wl_client_post_no_memory(client);
        free(frame);
        return NULL;
    }

    wl_resource_set_implementation(frame->resource, &frame_impl, frame,
        frame_handle_resource_destroy);
    frame->manager = manager_from_resource(manager_resource);
    if (!frame->manager) {
        gxde_screenshot_frame_v1_send_failed(frame->resource);
        wl_resource_set_user_data(frame->resource, NULL);
        free(frame);
        return NULL;
    }

    return frame;
}

static void frame_fail_and_forget(struct gxde_screenshot_frame* frame) {
    gxde_screenshot_frame_v1_send_failed(frame->resource);
    wl_resource_set_user_data(frame->resource, NULL);
    free(frame);
}

static void frame_handle_buffer_destroy(struct wl_listener* listener, void* data) {
    struct gxde_screenshot_frame* frame = wl_container_of(listener, frame, buffer_destroy);
    gxde_screenshot_frame_v1_send_cancelled(frame->resource);
    frame->source = NULL;
    gxde_screenshot_frame_destroy(frame);
}

static bool frame_buffer_get_params(struct wlr_buffer* buffer, uint32_t* format,
        uint32_t* n_planes, uint64_t* modifier, uint32_t* flags,
        struct gxde_screenshot_buffer_plane* planes) {
    struct wlr_dmabuf_attributes dmabuf;
    struct wlr_shm_attributes shm;

    if (!buffer) {
        return false;
    }

    if (wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
        if (dmabuf.n_planes == 0 || dmabuf.n_planes > WLR_DMABUF_MAX_PLANES) {
            return false;
        }
        *flags |= GXDE_SCREENSHOT_FRAME_V1_FLAGS_DMABUF;
        *format = dmabuf.format;
        *modifier = dmabuf.modifier;
        *n_planes = dmabuf.n_planes;
        for (uint32_t i = 0; i < *n_planes; i++) {
            planes[i].fd = dmabuf.fd[i];
            planes[i].offset = dmabuf.offset[i];
            planes[i].stride = dmabuf.stride[i];
        }
        return true;
    }

    if (wlr_buffer_get_shm(buffer, &shm)) {
        *format = shm.format;
        *modifier = 0;
        *n_planes = 1;
        planes[0].fd = shm.fd;
        planes[0].offset = shm.offset;
        planes[0].stride = shm.stride;
        return true;
    }

    return false;
}

static bool frame_handle_buffer_update_readback(struct gxde_screenshot_frame* frame,
        struct wlr_buffer* src) {
    /* 缩略图通常很小，把 GPU buffer(可能是 tiled dmabuf) 读回成 shm 缓冲。
     * 这样没有 EGL 的普通客户端(如 dock)直接 mmap fd 就能拿到像素，
     * 否则带 tiling 修饰符的 dmabuf 对客户端毫无用处。 */
    const int width = src->width;
    const int height = src->height;
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (frame->cpu_buffer && (frame->cpu_buffer->width != width ||
            frame->cpu_buffer->height != height)) {
        wlr_buffer_drop(frame->cpu_buffer);
        frame->cpu_buffer = NULL;
    }

    if (!frame->cpu_buffer) {
        frame->cpu_buffer = shm_create_buffer(width, height, DRM_FORMAT_ARGB8888);
    }
    if (!frame->cpu_buffer) {
        kywc_log(KYWC_WARN, "screenshot thumbnail: create shm buffer failed");
        return false;
    }

    void* dst_data = NULL;
    uint32_t dst_format = 0;
    size_t dst_stride = 0;
    if (!wlr_buffer_begin_data_ptr_access(frame->cpu_buffer,
            WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &dst_data, &dst_format, &dst_stride)) {
        kywc_log(KYWC_WARN, "screenshot thumbnail: data ptr access failed");
        return false;
    }

    bool ok = false;
    if (wlr_renderer_begin_with_buffer(frame->manager->server->renderer, src)) {
        ok = wlr_renderer_read_pixels(frame->manager->server->renderer, dst_format, dst_stride,
                width, height, 0, 0, 0, 0, dst_data);
        wlr_renderer_end(frame->manager->server->renderer);
    } else {
        kywc_log(KYWC_WARN, "screenshot thumbnail: begin_with_buffer failed");
    }

    wlr_buffer_end_data_ptr_access(frame->cpu_buffer);

    if (!ok) {
        kywc_log(KYWC_WARN, "screenshot thumbnail: read pixels failed");
        wlr_buffer_drop(frame->cpu_buffer);
        frame->cpu_buffer = NULL;
        return false;
    }

    return true;
}

static void frame_handle_buffer_update(struct wl_listener* listener, void* data) {
    struct gxde_screenshot_frame* frame = wl_container_of(listener, frame, buffer_update);
    struct wlr_buffer* buffer = NULL;
    uint32_t flags = 0;

    if (frame->source_type == GXDE_SCREENSHOT_SOURCE_CAPTURE) {
        struct capture_update_event* event = data;
        buffer = event->buffer;
        flags = event->buffer_changed ? 0 : GXDE_SCREENSHOT_FRAME_V1_FLAGS_REUSED;
    } else {
        struct thumbnail_update_event* event = data;
        buffer = event->buffer;
        flags = event->buffer_changed ? 0 : GXDE_SCREENSHOT_FRAME_V1_FLAGS_REUSED;
    }

    /* thumbnail: 优先发送 CPU 可读的 shm 拷贝 */
    struct wlr_buffer* send_buffer = buffer;
    if (frame->source_type == GXDE_SCREENSHOT_SOURCE_THUMBNAIL &&
            frame_handle_buffer_update_readback(frame, buffer)) {
        send_buffer = frame->cpu_buffer;
    }

    uint32_t format = 0;
    uint32_t n_planes = 1;
    uint64_t modifier = 0;
    struct gxde_screenshot_buffer_plane planes[WLR_DMABUF_MAX_PLANES] = { 0 };

    if (!frame_buffer_get_params(send_buffer, &format, &n_planes, &modifier, &flags, planes)) {
        gxde_screenshot_frame_v1_send_failed(frame->resource);
        return;
    }

    if (frame->buffer) {
        wlr_buffer_unlock(frame->buffer);
    }
    frame->buffer = wlr_buffer_lock(send_buffer);

    uint32_t modifier_hi = modifier >> 32;
    uint32_t modifier_lo = modifier & 0xFFFFFFFF;
    gxde_screenshot_frame_v1_send_buffer(frame->resource, planes[0].fd, format, send_buffer->width,
        send_buffer->height, planes[0].offset, planes[0].stride,
        modifier_hi, modifier_lo, flags);

    for (uint32_t i = 1; i < n_planes; i++) {
        gxde_screenshot_frame_v1_send_buffer_with_plane(frame->resource, i, planes[i].fd,
            planes[i].offset, planes[i].stride);
    }
    gxde_screenshot_frame_v1_send_buffer_done(frame->resource);

    if (frame->source_type == GXDE_SCREENSHOT_SOURCE_CAPTURE) {
        capture_mark_wants_update(frame->capture, false, false);
    } else {
        thumbnail_mark_wants_update(frame->thumbnail, false);
    }
}

static void frame_attach_capture(struct gxde_screenshot_frame* frame, struct capture* capture) {
    frame->source_type = GXDE_SCREENSHOT_SOURCE_CAPTURE;
    frame->capture = capture;

    wl_list_insert(&frame->manager->frames, &frame->link);

    frame->buffer_update.notify = frame_handle_buffer_update;
    capture_add_update_listener(frame->capture, &frame->buffer_update);
    frame->buffer_destroy.notify = frame_handle_buffer_destroy;
    capture_add_destroy_listener(frame->capture, &frame->buffer_destroy);
}

static void frame_attach_thumbnail(struct gxde_screenshot_frame* frame,
        struct thumbnail* thumbnail) {
    frame->source_type = GXDE_SCREENSHOT_SOURCE_THUMBNAIL;
    frame->thumbnail = thumbnail;

    wl_list_insert(&frame->manager->frames, &frame->link);

    frame->buffer_update.notify = frame_handle_buffer_update;
    thumbnail_add_update_listener(frame->thumbnail, &frame->buffer_update);
    frame->buffer_destroy.notify = frame_handle_buffer_destroy;
    thumbnail_add_destroy_listener(frame->thumbnail, &frame->buffer_destroy);
}

static uint32_t capture_options_from_cursor(uint32_t overlay_cursor) {
    return overlay_cursor ? CAPTURE_NEED_CURSOR : CAPTURE_NEED_NONE;
}

static uint32_t thumbnail_options_from_decoration(uint32_t without_decoration) {
    uint32_t options = THUMBNAIL_DISABLE_ROUND_CORNER | THUMBNAIL_ENABLE_SECURITY;
    options |= without_decoration ? THUMBNAIL_DISABLE_DECOR : THUMBNAIL_DISABLE_SHADOW;
    return options;
}

static struct kywc_output* find_enabled_output(const char* output) {
    if (!output || !*output) {
        return NULL;
    }

    struct kywc_output* kywc_output = kywc_output_by_uuid(output);
    if (!kywc_output || !kywc_output->state.enabled) {
        return NULL;
    }

    return kywc_output;
}

static struct kywc_view* find_mapped_window(const char* window, const char* app_id) {
    if (!window || !*window) {
        return NULL;
    }

    struct kywc_view* kywc_view = kywc_view_by_uuid(window);
    if (!kywc_view || !kywc_view->mapped) {
        return NULL;
    }

    if (app_id && *app_id) {
        if (!kywc_view->app_id || strcmp(kywc_view->app_id, app_id) != 0) {
            return NULL;
        }
    }

    return kywc_view;
}

static float thumbnail_scale_for_view(struct kywc_view* kywc_view, uint32_t max_width,
        uint32_t max_height) {
    if (kywc_view->geometry.width <= 0 || kywc_view->geometry.height <= 0) {
        return 0.0f;
    }

    float scale = 1.0f;
    if (max_width > 0 && max_width < (uint32_t)kywc_view->geometry.width) {
        scale = (float)max_width / (float)kywc_view->geometry.width;
    }
    if (max_height > 0 && max_height < (uint32_t)kywc_view->geometry.height) {
        float height_scale = (float)max_height / (float)kywc_view->geometry.height;
        if (height_scale < scale) {
            scale = height_scale;
        }
    }

    return scale;
}

static void manager_handle_capture_output(struct wl_client* client, struct wl_resource* resource,
        uint32_t id, const char* output, uint32_t overlay_cursor) {
    struct gxde_screenshot_frame* frame = frame_create(client, resource, id);
    if (!frame) {
        return;
    }

    struct kywc_output* kywc_output = find_enabled_output(output);
    if (!kywc_output) {
        frame_fail_and_forget(frame);
        return;
    }

    struct capture* capture = capture_create_from_output(
        output_from_kywc_output(kywc_output), capture_options_from_cursor(overlay_cursor));
    if (!capture) {
        frame_fail_and_forget(frame);
        return;
    }

    frame_attach_capture(frame, capture);
}

static bool output_relative_region(struct output* output, int32_t x, int32_t y, uint32_t width,
        uint32_t height, struct wlr_box* region) {
    if (!output) {
        return false;
    }
    if (width == 0 || height == 0 || width > INT_MAX || height > INT_MAX) {
        return false;
    }
    if (x < 0 || y < 0) {
        return false;
    }
    if ((int64_t)x + (int64_t)width > output->geometry.width ||
        (int64_t)y + (int64_t)height > output->geometry.height) {
        return false;
    }

    *region = (struct wlr_box){
        .x = output->geometry.x + x,
        .y = output->geometry.y + y,
        .width = width,
        .height = height,
    };

    return !wlr_box_empty(region);
}

static void manager_handle_capture_output_region(struct wl_client* client,
        struct wl_resource* resource, uint32_t id,
        const char* output, int32_t x, int32_t y,
        uint32_t width, uint32_t height,
        uint32_t overlay_cursor) {
    struct gxde_screenshot_frame* frame = frame_create(client, resource, id);
    if (!frame) {
        return;
    }

    struct kywc_output* kywc_output = find_enabled_output(output);
    if (!kywc_output) {
        frame_fail_and_forget(frame);
        return;
    }

    struct wlr_box region;
    if (!output_relative_region(output_from_kywc_output(kywc_output), x, y, width, height,
                                &region)) {
        frame_fail_and_forget(frame);
        return;
    }

    struct capture* capture =
        capture_create_from_area(&region, capture_options_from_cursor(overlay_cursor));
    if (!capture) {
        frame_fail_and_forget(frame);
        return;
    }

    frame_attach_capture(frame, capture);
}

static void manager_handle_capture_window(struct wl_client* client, struct wl_resource* resource,
        uint32_t id, const char* window, uint32_t without_decoration) {
    struct gxde_screenshot_frame* frame = frame_create(client, resource, id);
    if (!frame) {
        return;
    }

    struct kywc_view* kywc_view = find_mapped_window(window, NULL);
    if (!kywc_view) {
        frame_fail_and_forget(frame);
        return;
    }

    struct thumbnail* thumbnail = thumbnail_create_from_view(
        view_from_kywc_view(kywc_view), thumbnail_options_from_decoration(without_decoration), 1.0);
    if (!thumbnail) {
        frame_fail_and_forget(frame);
        return;
    }

    frame_attach_thumbnail(frame, thumbnail);
}

static void manager_handle_capture_window_thumbnail(struct wl_client* client,
        struct wl_resource* resource, uint32_t id, const char* app_id, const char* window,
        uint32_t max_width, uint32_t max_height, uint32_t without_decoration) {
    struct gxde_screenshot_frame* frame = frame_create(client, resource, id);
    if (!frame) {
        return;
    }

    struct kywc_view* kywc_view = find_mapped_window(window, app_id);
    if (!kywc_view) {
        frame_fail_and_forget(frame);
        return;
    }

    float scale = thumbnail_scale_for_view(kywc_view, max_width, max_height);
    if (scale <= 0.0f) {
        frame_fail_and_forget(frame);
        return;
    }

    struct thumbnail* thumbnail =
        thumbnail_create_from_view(view_from_kywc_view(kywc_view),
                                   thumbnail_options_from_decoration(without_decoration), scale);
    if (!thumbnail) {
        frame_fail_and_forget(frame);
        return;
    }

    frame_attach_thumbnail(frame, thumbnail);
}

static void manager_handle_destroy(struct wl_client* client, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static const struct gxde_screenshot_manager_v1_interface gxde_screenshot_manager_impl = {
    .capture_output = manager_handle_capture_output,
    .capture_output_region = manager_handle_capture_output_region,
    .capture_window = manager_handle_capture_window,
    .capture_window_thumbnail = manager_handle_capture_window_thumbnail,
    .destroy = manager_handle_destroy,
};

static void manager_handle_resource_destroy(struct wl_resource* resource) {
    struct gxde_screenshot_manager_resource* manager_resource = wl_resource_get_user_data(resource);
    if (!manager_resource) {
        return;
    }

    wl_list_remove(&manager_resource->link);
    free(manager_resource);
}

static void gxde_screenshot_manager_bind(struct wl_client* client, void* data, uint32_t version,
                                         uint32_t id) {
    struct gxde_screenshot_manager* manager = data;

    struct gxde_screenshot_manager_resource* manager_resource =
        calloc(1, sizeof(*manager_resource));
    if (!manager_resource) {
        wl_client_post_no_memory(client);
        return;
    }

    struct wl_resource* resource =
        wl_resource_create(client, &gxde_screenshot_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        free(manager_resource);
        return;
    }

    manager_resource->resource = resource;
    manager_resource->manager = manager;
    wl_list_insert(&manager->resources, &manager_resource->link);

    wl_resource_set_implementation(resource, &gxde_screenshot_manager_impl, manager_resource,
        manager_handle_resource_destroy);
}

static void handle_server_destroy(struct wl_listener* listener, void* data) {
    struct gxde_screenshot_manager* manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    wl_list_init(&manager->server_destroy.link);
    if (!wl_list_empty(&manager->display_destroy.link)) {
        wl_list_remove(&manager->display_destroy.link);
        wl_list_init(&manager->display_destroy.link);
    }
    if (manager->global) {
        wl_global_destroy(manager->global);
        manager->global = NULL;
    }
    gxde_screenshot_manager_destroy_frames(manager);
    gxde_screenshot_manager_destroy_resources(manager);
    free(manager);
}

static void handle_display_destroy(struct wl_listener* listener, void* data) {
    struct gxde_screenshot_manager* manager = wl_container_of(listener, manager, display_destroy);
    wl_list_remove(&manager->display_destroy.link);
    wl_list_init(&manager->display_destroy.link);
    if (manager->global) {
        wl_global_destroy(manager->global);
        manager->global = NULL;
    }
    gxde_screenshot_manager_destroy_frames(manager);
    gxde_screenshot_manager_destroy_resources(manager);
}

bool gxde_screenshot_manager_create(struct server* server) {
    struct gxde_screenshot_manager* manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    manager->server = server;
    wl_list_init(&manager->resources);
    wl_list_init(&manager->frames);
    wl_list_init(&manager->server_destroy.link);
    wl_list_init(&manager->display_destroy.link);

    manager->global =
        wl_global_create(server->display, &gxde_screenshot_manager_v1_interface,
            GXDE_SCREENSHOT_MANAGER_VERSION, manager, gxde_screenshot_manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_WARN, "GXDE screenshot manager create failed");
        free(manager);
        return false;
    }

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    return true;
}
