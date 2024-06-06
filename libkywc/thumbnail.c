// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include "libkywc_p.h"

static struct ky_thumbnail *thumbnail_from_kywc_thumbnail(kywc_thumbnail *kywc_thumbnail)
{
    struct ky_thumbnail *thumbnail = wl_container_of(kywc_thumbnail, thumbnail, base);
    return thumbnail;
}

void ky_thumbnail_destroy(struct ky_thumbnail *thumbnail)
{
    if (thumbnail->impl && thumbnail->impl->destroy) {
        thumbnail->impl->destroy(&thumbnail->base, thumbnail->user_data);
    }

    if (thumbnail->destroy) {
        thumbnail->destroy(thumbnail);
    }

    wl_list_remove(&thumbnail->link);

    free((void *)thumbnail->base.source_uuid);
    free((void *)thumbnail->base.output_uuid);
    free(thumbnail);
}

void ky_thumbnail_update_buffer(struct ky_thumbnail *thumbnail,
                                const struct kywc_thumbnail_buffer *buffer, bool *want_buffer)
{
    if (thumbnail->impl && thumbnail->impl->buffer) {
        *want_buffer = thumbnail->impl->buffer(&thumbnail->base, buffer, thumbnail->user_data);
    }
}

struct ky_thumbnail_manager *ky_thumbnail_manager_create(kywc_context *ctx)
{
    struct ky_thumbnail_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return NULL;
    }

    manager->ctx = ctx;
    wl_list_init(&manager->thumbnails);

    return manager;
}

void ky_thumbnail_manager_destroy(struct ky_thumbnail_manager *manager)
{
    if (!manager) {
        return;
    }

    // destroy all thumbnails
    struct ky_thumbnail *thumbnail, *tmp;
    wl_list_for_each_safe(thumbnail, tmp, &manager->thumbnails, link) {
        ky_thumbnail_destroy(thumbnail);
    }

    if (manager->destroy) {
        manager->destroy(manager);
    }

    free(manager);
}

kywc_thumbnail *kywc_thumbnail_create(kywc_context *ctx, enum kywc_thumbnail_type type,
                                      const char *source_uuid, const char *output_uuid,
                                      const struct kywc_thumbnail_interface *impl, void *data)
{
    if (!ctx || !ctx->thumbnail) {
        return NULL;
    }

    struct ky_thumbnail *thumbnail = calloc(1, sizeof(*thumbnail));
    if (!thumbnail) {
        return NULL;
    }

    struct ky_thumbnail_manager *manager = ctx->thumbnail;
    thumbnail->manager = manager;
    wl_list_insert(&manager->thumbnails, &thumbnail->link);

    thumbnail->base.type = type;
    thumbnail->base.source_uuid = strdup(source_uuid);
    if (output_uuid) {
        thumbnail->base.output_uuid = strdup(output_uuid);
    }

    thumbnail->impl = impl;
    thumbnail->user_data = data;

    if (type == KYWC_THUMBNAIL_TYPE_OUTPUT && manager->capture_output) {
        manager->capture_output(manager, thumbnail, source_uuid);
    } else if (type == KYWC_THUMBNAIL_TYPE_TOPLEVEL && manager->capture_toplevel) {
        manager->capture_toplevel(manager, thumbnail, source_uuid, false);
    } else if (type == KYWC_THUMBNAIL_TYPE_WORKSPACE && manager->capture_workspace) {
        manager->capture_workspace(manager, thumbnail, source_uuid, output_uuid);
    }

    return &thumbnail->base;
}

kywc_thumbnail *kywc_thumbnail_create_from_output(kywc_context *ctx, const char *source_uuid,
                                                  const struct kywc_thumbnail_interface *impl,
                                                  void *data)
{
    return kywc_thumbnail_create(ctx, KYWC_THUMBNAIL_TYPE_OUTPUT, source_uuid, NULL, impl, data);
}

kywc_thumbnail *kywc_thumbnail_create_from_workspace(kywc_context *ctx, const char *source_uuid,
                                                     const char *output_uuid,
                                                     const struct kywc_thumbnail_interface *impl,
                                                     void *data)
{
    return kywc_thumbnail_create(ctx, KYWC_THUMBNAIL_TYPE_WORKSPACE, source_uuid, output_uuid, impl,
                                 data);
}

kywc_thumbnail *kywc_thumbnail_create_from_toplevel(kywc_context *ctx, const char *source_uuid,
                                                    bool without_decoration,
                                                    const struct kywc_thumbnail_interface *impl,
                                                    void *data)
{
    if (!ctx || !ctx->thumbnail) {
        return NULL;
    }

    struct ky_thumbnail *thumbnail = calloc(1, sizeof(*thumbnail));
    if (!thumbnail) {
        return NULL;
    }

    struct ky_thumbnail_manager *manager = ctx->thumbnail;
    thumbnail->manager = manager;
    wl_list_insert(&manager->thumbnails, &thumbnail->link);

    thumbnail->base.type = KYWC_THUMBNAIL_TYPE_TOPLEVEL;
    thumbnail->base.source_uuid = strdup(source_uuid);

    thumbnail->impl = impl;
    thumbnail->user_data = data;

    if (manager->capture_toplevel) {
        manager->capture_toplevel(manager, thumbnail, source_uuid, without_decoration);
    }

    return &thumbnail->base;
}

kywc_context *kywc_thumbnail_get_context(kywc_thumbnail *thumbnail)
{
    struct ky_thumbnail *ky_thumbnail = thumbnail_from_kywc_thumbnail(thumbnail);
    return ky_thumbnail->manager->ctx;
}

void kywc_thumbnail_set_user_data(kywc_thumbnail *thumbnail, void *data)
{
    struct ky_thumbnail *ky_thumbnail = thumbnail_from_kywc_thumbnail(thumbnail);
    ky_thumbnail->user_data = data;
}

void *kywc_thumbnail_get_user_data(kywc_thumbnail *thumbnail)
{
    struct ky_thumbnail *ky_thumbnail = thumbnail_from_kywc_thumbnail(thumbnail);
    return ky_thumbnail->user_data;
}

void kywc_thumbnail_destroy(kywc_thumbnail *thumbnail)
{
    struct ky_thumbnail *ky_thumbnail = thumbnail_from_kywc_thumbnail(thumbnail);
    ky_thumbnail_destroy(ky_thumbnail);
}
