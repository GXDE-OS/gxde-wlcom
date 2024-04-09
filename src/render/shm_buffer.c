// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/drm_format_set.h>

#include <kywc/identifier.h>
#include <kywc/log.h>

#include "render/pixel_format.h"
#include "renderer_p.h"

struct shm_buffer {
    struct wlr_buffer base;
    struct wlr_shm_attributes shm;
    void *data;
    size_t size;
};

static int excl_shm_open(char *name)
{
    int retries = 100;
    do {
        kywc_identifier_rand_generate(name, 0);

        --retries;
        // CLOEXEC is guaranteed to be set by shm_open
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);

    return -1;
}

static int allocate_shm_file(size_t size)
{
    char name[] = "/kywc-XXXXXX";
    int fd = excl_shm_open(name);
    if (fd < 0) {
        return -1;
    }
    shm_unlink(name);

    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static struct shm_buffer *shm_buffer_from_buffer(struct wlr_buffer *wlr_buffer)
{
    struct shm_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
    return buffer;
}

static void shm_buffer_destroy(struct wlr_buffer *wlr_buffer)
{
    struct shm_buffer *buffer = shm_buffer_from_buffer(wlr_buffer);
    munmap(buffer->data, buffer->size);
    close(buffer->shm.fd);
    free(buffer);
}

static bool shm_buffer_get_shm(struct wlr_buffer *wlr_buffer, struct wlr_shm_attributes *shm)
{
    struct shm_buffer *buffer = shm_buffer_from_buffer(wlr_buffer);
    *shm = buffer->shm;
    return true;
}

static bool shm_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer, uint32_t flags,
                                             void **data, uint32_t *format, size_t *stride)
{
    struct shm_buffer *buffer = shm_buffer_from_buffer(wlr_buffer);
    *data = buffer->data;
    *format = buffer->shm.format;
    *stride = buffer->shm.stride;
    return true;
}

static void shm_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer)
{
    // This space is intentionally left blank
}

static const struct wlr_buffer_impl buffer_impl = {
    .destroy = shm_buffer_destroy,
    .get_shm = shm_buffer_get_shm,
    .begin_data_ptr_access = shm_buffer_begin_data_ptr_access,
    .end_data_ptr_access = shm_buffer_end_data_ptr_access,
};

struct wlr_buffer *shm_create_buffer(int width, int height, uint32_t format)
{
    const struct ky_pixel_format *info = ky_pixel_format_from_drm(format);
    if (info == NULL) {
        kywc_log(KYWC_ERROR, "Unsupported pixel format 0x%" PRIX32, format);
        return NULL;
    }

    struct shm_buffer *buffer = calloc(1, sizeof(*buffer));
    if (buffer == NULL) {
        return NULL;
    }
    wlr_buffer_init(&buffer->base, &buffer_impl, width, height);

    int stride = ky_pixel_format_min_stride(info, width);
    buffer->size = stride * height;
    buffer->shm.fd = allocate_shm_file(buffer->size);
    if (buffer->shm.fd < 0) {
        free(buffer);
        return NULL;
    }

    buffer->shm.format = format;
    buffer->shm.width = width;
    buffer->shm.height = height;
    buffer->shm.stride = stride;
    buffer->shm.offset = 0;

    buffer->data = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE, MAP_SHARED, buffer->shm.fd, 0);
    if (buffer->data == MAP_FAILED) {
        kywc_log_errno(KYWC_ERROR, "mmap failed");
        close(buffer->shm.fd);
        free(buffer);
        return NULL;
    }

    return &buffer->base;
}
