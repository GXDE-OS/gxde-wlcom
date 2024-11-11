// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include <jpeglib.h>
#include <librsvg/rsvg.h>

#include "painter_p.h"

bool cairo_buffer_draw_svg(struct cairo_buffer *buffer, const char *data, struct kywc_box *box)
{
    size_t size = strlen(data);
    // check signature, this an xml, so skip spaces from the start
    while (size && isspace(*data) != 0) {
        ++data;
        --size;
    }

    const uint8_t signature[] = { '<' };
    if (size <= sizeof(signature) || memcmp(data, signature, sizeof(signature))) {
        return false;
    }

    GError *err = NULL;
    RsvgHandle *svg = rsvg_handle_new_from_data((guint8 *)data, size, &err);
    if (!svg) {
        kywc_log(KYWC_ERROR, "Invalid SVG format");
        if (err && err->message) {
            kywc_log(KYWC_ERROR, "%s: %s", data, err->message);
        }
        return false;
    }

    RsvgRectangle viewport = {
        .x = box->x,
        .y = box->y,
        .width = box->width,
        .height = box->height,
    };

    // render svg to surface
    gboolean ok = rsvg_handle_render_document(svg, buffer->cairo, &viewport, &err);
    g_object_unref(svg);
    if (!ok && err && err->message) {
        kywc_log(KYWC_ERROR, "%s", err->message);
    }
    return ok;
}

static uint8_t *do_decode_jpeg(void *data, size_t size, uint32_t *width, uint32_t *height)
{
    // check signature
    const uint8_t signature[] = { 0xff, 0xd8 };
    if (size <= sizeof(signature) || memcmp(data, signature, sizeof(signature))) {
        return NULL;
    }

    struct jpeg_decompress_struct jpg;
    struct jpeg_error_mgr err;
    jpg.err = jpeg_std_error(&err);

    jpeg_create_decompress(&jpg);
    jpeg_mem_src(&jpg, data, size);
    jpeg_read_header(&jpg, TRUE);
    jpeg_start_decompress(&jpg);
#ifdef LIBJPEG_TURBO_VERSION
    jpg.out_color_space = JCS_EXT_BGRA;
#endif // LIBJPEG_TURBO_VERSION

    uint32_t *buffer = calloc(jpg.output_width * jpg.output_height, 4);
    if (!buffer) {
        jpeg_destroy_decompress(&jpg);
        return NULL;
    }

    while (jpg.output_scanline < jpg.output_height) {
        uint8_t *line = (uint8_t *)&buffer[jpg.output_scanline * jpg.output_width];
        jpeg_read_scanlines(&jpg, &line, 1);

        // convert grayscale to argb
        if (jpg.out_color_components == 1) {
            uint32_t *pixel = (uint32_t *)line;
            for (int x = jpg.output_width - 1; x >= 0; --x) {
                const uint8_t src = *(line + x);
                pixel[x] = ((uint32_t)0xff << 24) | (uint32_t)src << 16 | (uint32_t)src << 8 | src;
            }
        }

#ifndef LIBJPEG_TURBO_VERSION
        //  convert rgb to argb
        if (jpg.out_color_components == 3) {
            uint32_t *pixel = (uint32_t *)line;
            for (int x = jpg.output_width - 1; x >= 0; --x) {
                const uint8_t *src = line + x * 3;
                pixel[x] = ((uint32_t)0xff << 24) | (uint32_t)src[0] << 16 | (uint32_t)src[1] << 8 |
                           src[2];
            }
        }
#endif // LIBJPEG_TURBO_VERSION
    }

    jpeg_finish_decompress(&jpg);
    jpeg_destroy_decompress(&jpg);

    *width = jpg.output_width;
    *height = jpg.output_height;

    return (uint8_t *)buffer;
}

uint8_t *decode_jpeg(const char *file, uint32_t *width, uint32_t *height)
{
    int fd = open(file, O_RDONLY);
    if (fd == -1) {
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        close(fd);
        return NULL;
    }

    void *data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    uint8_t *buffer = do_decode_jpeg(data, st.st_size, width, height);

    munmap(data, st.st_size);
    close(fd);

    return buffer;
}
