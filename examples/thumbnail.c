// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdio.h>
#include <string.h>

#include "buffer.h"

static bool thumbnail_handle_buffer(kywc_thumbnail *thumbnail,
                                    const struct kywc_thumbnail_buffer *buffer, void *data)
{
    printf("thumbnail %d \"%s\"\n", thumbnail->type, thumbnail->source_uuid);
    printf("  %s: %d reused: %s\n",
           buffer->flags & KYWC_THUMBNAIL_BUFFER_IS_DMABUF ? "dmabuf" : "memfd", buffer->fd,
           buffer->flags & KYWC_THUMBNAIL_BUFFER_IS_REUSED ? "yes" : "no");
    printf("  format: 0x%x\n", buffer->format);
    printf("  size: %d x %d\n", buffer->width, buffer->height);
    printf("  offset: %d  stride:  %d\n", buffer->offset, buffer->stride);
    printf("  modifier: 0x%lx\n", buffer->modifier);
    printf("\n");

    kywc_context *ctx = kywc_thumbnail_get_context(thumbnail);
    struct kywc_buffer_helper *helper = kywc_context_get_user_data(ctx);
    if (!helper) {
        return false;
    }

    struct kywc_buffer *kywc_buffer =
        kywc_buffer_hepler_import_thumbnail(helper, thumbnail, buffer);
    if (!kywc_buffer) {
        return false;
    }

    kywc_thumbnail_set_user_data(thumbnail, kywc_buffer);
#if 0
    static int index = 0;
    char path[256];
    // files are named with YUView format
    snprintf(path, 256, "%s-%d_%dx%d_bgra.rgba", toplevel->app_id, index++, buffer->width,
             buffer->height);
    kywc_buffer_write_to_file(kywc_buffer, path);
#else
    kywc_buffer_show_in_window(kywc_buffer, thumbnail->source_uuid);
#endif
    // return false if oneshot
    return true;
}

static void thumbnail_handle_destroy(kywc_thumbnail *thumbnail, void *data)
{
    printf("thumbnail for %s is gone\n", thumbnail->source_uuid);
    struct kywc_buffer *kywc_buffer = kywc_thumbnail_get_user_data(thumbnail);
    kywc_buffer_destroy(kywc_buffer);
}

static struct kywc_thumbnail_interface thumbnail_impl = {
    .buffer = thumbnail_handle_buffer,
    .destroy = thumbnail_handle_destroy,
};

static void handle_destroy(kywc_context *ctx, void *data)
{
    struct kywc_buffer_helper *helper = data;
    kywc_buffer_helper_destroy(helper);
}

static void handle_create(kywc_context *ctx, void *data)
{
    struct kywc_buffer_helper *helper = kywc_buffer_helper_create(ctx);
    if (!helper) {
        return;
    }

    kywc_context_set_user_data(ctx, helper);
}

static struct kywc_context_interface context_impl = {
    .create = handle_create,
    .destroy = handle_destroy,
};

int main(int argc, char *argv[])
{
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "please input args\n");
        return -1;
    }

    enum kywc_thumbnail_type type = 0;
    const char *source_id, *output_id = NULL;

    const char *option = argv[1];
    if (strcmp(option, "output") == 0) {
        type = KYWC_THUMBNAIL_TYPE_OUTPUT;
        source_id = argv[2];
    } else if (strcmp(option, "toplevel") == 0) {
        type = KYWC_THUMBNAIL_TYPE_TOPLEVEL;
        source_id = argv[2];
    } else if (strcmp(option, "workspace") == 0) {
        if (argc != 4) {
            fprintf(stderr, "please input workspace id and output id both\n");
            return -1;
        }
        type = KYWC_THUMBNAIL_TYPE_WORKSPACE;
        source_id = argv[2];
        output_id = argv[3];
    } else {
        fprintf(stderr, "please input either output, toplvel or workspace\n");
        return -1;
    }

    uint32_t caps = KYWC_CONTEXT_CAPABILITY_THUMBNAIL;
    kywc_context *ctx = kywc_context_create(NULL, caps, &context_impl, NULL);
    if (!ctx) {
        return -1;
    }

    if (kywc_thumbnail_create(ctx, type, source_id, output_id, &thumbnail_impl, NULL)) {
        kywc_context_dispatch(ctx);
    }

    kywc_context_destroy(ctx);

    return 0;
}
