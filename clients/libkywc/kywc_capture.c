// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kywc-capture-v1-client-protocol.h"

#include "libkywc_p.h"

bool _kywc_capture_init(kywc_context *ctx, enum kywc_context_capability capability);

static void frame_handle_failed(void *data, struct kywc_capture_frame_v1 *kywc_capture_frame_v1)
{
    struct ky_thumbnail *thumbnail = data;
    ky_thumbnail_destroy(thumbnail);
}

static void frame_handle_cancelled(void *data, struct kywc_capture_frame_v1 *kywc_capture_frame_v1)
{
    struct ky_thumbnail *thumbnail = data;
    ky_thumbnail_destroy(thumbnail);
}

static void frame_handle_buffer(void *data, struct kywc_capture_frame_v1 *kywc_capture_frame_v1,
                                int32_t fd, uint32_t format, uint32_t width, uint32_t height,
                                uint32_t offset, uint32_t stride, uint32_t modifier_hi,
                                uint32_t modifier_lo, uint32_t flags)
{
    struct ky_thumbnail *thumbnail = data;
    bool want_buffer = false;

    struct kywc_thumbnail_buffer buffer = {
        .fd = fd,
        .format = format,
        .width = width,
        .height = height,
        .offset = offset,
        .stride = stride,
        .modifier = (uint64_t)modifier_hi << 32 | modifier_lo,
        .flags = flags,
    };

    ky_thumbnail_update_buffer(thumbnail, &buffer, &want_buffer);

    kywc_capture_frame_v1_release_buffer(kywc_capture_frame_v1, want_buffer);
    wl_display_flush(thumbnail->manager->ctx->display);
    close(fd);

    if (!want_buffer) {
        ky_thumbnail_destroy(thumbnail);
    }
}

static const struct kywc_capture_frame_v1_listener frame_listener = {
    .failed = frame_handle_failed,
    .cancelled = frame_handle_cancelled,
    .buffer = frame_handle_buffer,
};

static void frame_destroy(struct ky_thumbnail *thumbnail)
{
    struct kywc_capture_frame_v1 *frame = thumbnail->data;
    kywc_capture_frame_v1_destroy(frame);
    wl_display_flush(thumbnail->manager->ctx->display);
}

static void manager_capture_output(struct ky_thumbnail_manager *manager,
                                   struct ky_thumbnail *thumbnail, const char *uuid)
{
    struct kywc_capture_manager_v1 *thumbnail_manager = manager->data;
    struct kywc_capture_frame_v1 *frame =
        kywc_capture_manager_v1_capture_output(thumbnail_manager, 0, uuid);
    kywc_capture_frame_v1_add_listener(frame, &frame_listener, thumbnail);
    wl_display_flush(manager->ctx->display);
    thumbnail->destroy = frame_destroy;
    thumbnail->data = frame;
}

static void manager_capture_workspace(struct ky_thumbnail_manager *manager,
                                      struct ky_thumbnail *thumbnail, const char *uuid,
                                      const char *output)
{
    struct kywc_capture_manager_v1 *thumbnail_manager = manager->data;
    struct kywc_capture_frame_v1 *frame =
        kywc_capture_manager_v1_capture_workspace(thumbnail_manager, uuid, output);
    kywc_capture_frame_v1_add_listener(frame, &frame_listener, thumbnail);
    wl_display_flush(manager->ctx->display);
    thumbnail->destroy = frame_destroy;
    thumbnail->data = frame;
}

static void manager_capture_toplevel(struct ky_thumbnail_manager *manager,
                                     struct ky_thumbnail *thumbnail, const char *uuid)
{
    struct kywc_capture_manager_v1 *thumbnail_manager = manager->data;
    struct kywc_capture_frame_v1 *frame =
        kywc_capture_manager_v1_capture_toplevel(thumbnail_manager, uuid);
    kywc_capture_frame_v1_add_listener(frame, &frame_listener, thumbnail);
    wl_display_flush(manager->ctx->display);
    thumbnail->destroy = frame_destroy;
    thumbnail->data = frame;
}

static void manager_destroy(struct ky_thumbnail_manager *manager)
{
    struct kywc_capture_manager_v1 *thumbnail_manager = manager->data;
    kywc_capture_manager_v1_destroy(thumbnail_manager);
    wl_display_flush(manager->ctx->display);
}

static bool thumbnail_provider_bind(struct ky_context_provider *provider,
                                    struct wl_registry *registry, uint32_t name,
                                    const char *interface, uint32_t version)
{
    if (strcmp(interface, kywc_capture_manager_v1_interface.name) == 0) {
        uint32_t version_to_bind = version <= 1 ? version : 1;
        struct ky_thumbnail_manager *manager = provider->data;
        struct kywc_capture_manager_v1 *thumbnail_manager =
            wl_registry_bind(registry, name, &kywc_capture_manager_v1_interface, version_to_bind);
        kywc_capture_manager_v1_set_user_data(thumbnail_manager, manager);
        manager->capture_output = manager_capture_output;
        manager->capture_workspace = manager_capture_workspace;
        manager->capture_toplevel = manager_capture_toplevel;
        manager->destroy = manager_destroy;
        manager->data = thumbnail_manager;
        return true;
    }

    return false;
}

static void thumbnail_provider_destroy(struct ky_context_provider *provider)
{
    struct ky_thumbnail_manager *manager = provider->data;
    ky_thumbnail_manager_destroy(manager);
    free(provider);
}

bool _kywc_capture_init(kywc_context *ctx, enum kywc_context_capability capability)
{
    struct ky_context_provider *provider = calloc(1, sizeof(*provider));
    if (!provider) {
        return false;
    }

    wl_list_init(&provider->link);
    provider->capability = capability;
    provider->bind = thumbnail_provider_bind;
    provider->destroy = thumbnail_provider_destroy;

    struct ky_thumbnail_manager *manager = ky_thumbnail_manager_create(ctx);
    if (!manager) {
        free(provider);
        return false;
    }

    provider->data = manager;

    if (!ky_context_add_provider(ctx, provider, manager)) {
        free(manager);
        free(provider);
        return false;
    }

    return true;
}
