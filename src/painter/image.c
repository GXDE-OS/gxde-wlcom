// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include <jpeglib.h>
#include <png.h>

#include "painter_p.h"
#include "util/file.h"

static uint8_t *do_decode_jpeg(const uint8_t *data, size_t size, int *width, int *height)
{
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

    uint32_t *buffer = malloc(jpg.output_width * jpg.output_height * 4);
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

// PNG memory reader
struct mem_reader {
    const uint8_t *data;
    const size_t size;
    size_t position;
};

static void png_reader(png_structp png, png_bytep buffer, size_t size)
{
    struct mem_reader *reader = (struct mem_reader *)png_get_io_ptr(png);
    if (reader && reader->position + size < reader->size) {
        memcpy(buffer, reader->data + reader->position, size);
        reader->position += size;
    } else {
        png_error(png, "No data in PNG reader");
    }
}

static uint8_t *do_decode_png(const uint8_t *data, size_t size, int *width, int *height)
{
    png_struct *png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        return NULL;
    }
    png_info *info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        return NULL;
    }

    struct mem_reader reader = { .data = data, .size = size, .position = 0 };
    // get general image info
    png_set_read_fn(png, &reader, &png_reader);
    png_read_info(png, info);

    int w = png_get_image_width(png, info);
    int h = png_get_image_height(png, info);

    uint8_t *buffer = malloc(w * h * 4);
    if (!buffer) {
        png_destroy_read_struct(&png, &info, NULL);
        return NULL;
    }

    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    // setup decoder
    if (png_get_interlace_type(png, info) != PNG_INTERLACE_NONE) {
        png_set_interlace_handling(png);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
        if (bit_depth < 8) {
            png_set_expand_gray_1_2_4_to_8(png);
        }
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }
    if (bit_depth == 16) {
        png_set_strip_16(png);
    }

    png_set_filler(png, 0xff, PNG_FILLER_AFTER);
    png_set_alpha_mode(png, PNG_ALPHA_PREMULTIPLIED, PNG_GAMMA_LINEAR);
    png_set_packing(png);
    png_set_packswap(png);
    png_set_bgr(png);
    png_set_expand(png);
    png_read_update_info(png, info);

    png_bytep *row_ptrs = malloc(h * sizeof(*row_ptrs));
    if (!row_ptrs) {
        free(buffer);
        png_destroy_read_struct(&png, &info, NULL);
        return NULL;
    }

    for (int i = 0; i < h; i++) {
        row_ptrs[i] = buffer + w * 4 * i;
    }

    png_read_image(png, row_ptrs);
    png_destroy_read_struct(&png, &info, NULL);
    free(row_ptrs);

    *width = w;
    *height = h;

    return buffer;
}

typedef struct __attribute__((__packed__)) {
    uint16_t type;
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} bmp_file_header;

typedef struct __attribute__((__packed__)) {
    uint32_t dib_size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t img_size;
    uint32_t hres;
    uint32_t vres;
    uint32_t clr_palette;
    uint32_t clr_important;
} bmp_info_header;

static bool image_is_png(const uint8_t *data)
{
    return data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47 &&
           data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A;
}

static uint8_t *do_decode_bmp(const uint8_t *data, const uint8_t *pixel, int *width, int *height)
{
    const bmp_info_header *header = (bmp_info_header *)data;
    if (header->bpp != 32 || header->compression != 0) {
        return NULL;
    }

    uint32_t stride = header->width * 4;
    // The height of the BMP image must be twice the height in ico
    uint32_t h = pixel ? abs(header->height) : abs(header->height) / 2;

    uint8_t *buffer = malloc(stride * h);
    if (!buffer) {
        return NULL;
    }

    const uint8_t *src = pixel ? pixel : data + sizeof(*header);
    // default is bottom-to-top
    if (header->height < 0) {
        memcpy(buffer, src, stride * h);
    } else {
        for (uint32_t i = 0; i < h; i++) {
            memcpy(buffer + i * stride, src + (h - 1 - i) * stride, stride);
        }
    }

    *width = header->width;
    *height = h;

    return buffer;
}

static uint8_t *do_decode_ico(const uint8_t *data, size_t size, int *width, int *height)
{
    struct __attribute__((__packed__)) ico_header {
        uint16_t reserved;
        uint16_t type; // 1 for icon, 2 for cursor
        uint16_t count;
    };

    struct __attribute__((__packed__)) ico_directory {
        uint8_t width;  // should be 0 if 256 pixels
        uint8_t height; // should be 0 if 256 pixels
        uint8_t count;  // should be 0 if more than 256 colors
        uint8_t reserved;
        uint16_t planes; // should be 0 or 1
        uint16_t bpp;
        uint32_t size;
        uint32_t offset;
    };

    struct __attribute__((__packed__)) ico {
        struct ico_header header;
        struct ico_directory dict[];
    };

    const struct ico *ico = (struct ico *)data;
    // header is checked in image_decode_file
    const struct ico_directory *best_dict = NULL, *max_dict = NULL;
    // use max size 256 if not set
    uint32_t best_width = *width ? *width : 256;
    uint32_t min_width = UINT_MAX, max_width = 0;

    for (uint16_t i = 0; i < ico->header.count; i++) {
        const struct ico_directory *dict = &ico->dict[i];
        if (dict->bpp != 32) {
            continue;
        }
        uint32_t width = dict->width ? dict->width : 256;
        if (width == best_width) {
            best_dict = dict;
            break;
        }
        if (width > best_width && width < min_width) {
            min_width = width;
            best_dict = dict;
        }
        if (width > max_width) {
            max_width = width;
            max_dict = dict;
        }
    }

    if (!best_dict && max_dict) {
        best_dict = max_dict;
    }
    if (!best_dict) {
        return NULL;
    }

    const uint8_t *pixels = data + best_dict->offset;

    if (image_is_png(pixels)) {
        return do_decode_png(pixels, best_dict->size, width, height);
    } else if (*(uint32_t *)pixels == 0x28) {
        // it must exclude the opening BITMAPFILEHEADER structure
        return do_decode_bmp(pixels, NULL, width, height);
    } else {
        return NULL;
    }
}

uint8_t *image_read_from_file(const char *filename, int *width, int *height)
{
    struct file *file = file_open(filename, "\n", NULL);
    if (!file) {
        return NULL;
    }

    size_t size = 0;
    uint8_t *data = (uint8_t *)file_get_data(file, &size);
    if (size <= 8) {
        file_close(file);
        return NULL;
    }

    uint8_t *buffer = NULL;

    if (image_is_png(data)) {
        buffer = do_decode_png(data, size, width, height);
    } else if (data[0] == 0xFF && data[1] == 0xD8) {
        buffer = do_decode_jpeg(data, size, width, height);
    } else if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01 && data[3] == 0x00) {
        buffer = do_decode_ico(data, size, width, height);
    } else if (data[0] == 0x42 && data[1] == 0x4D &&
               *(uint32_t *)(data + sizeof(bmp_file_header)) == 0x28) {
        buffer = do_decode_bmp(data + sizeof(bmp_file_header),
                               data + ((bmp_file_header *)data)->offset, width, height);
    } else {
        kywc_log(KYWC_WARN, "%s: unsupported image format", filename);
    }

    file_close(file);
    return buffer;
}

static bool do_encode_bmp(FILE *file, int width, int height, size_t stride, uint8_t *data)
{
    bmp_info_header info_header = {
        .dib_size = sizeof(info_header),
        .width = width,
        .height = -height,
        .planes = 1,
        .bpp = 32,
        .img_size = stride * height,
    };

    bmp_file_header file_header = {
        .type = 'B' | ('M' << 8),
        .file_size = sizeof(file_header) + sizeof(info_header) + info_header.img_size,
        .offset = sizeof(file_header) + sizeof(info_header),
    };

    fwrite(&file_header, sizeof(file_header), 1, file);
    fwrite(&info_header, sizeof(info_header), 1, file);
    fwrite(data, stride, height, file);

    return true;
}

static bool do_encode_png(FILE *file, int width, int height, size_t stride, uint8_t *data)
{
    png_struct *png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        return false;
    }
    png_info *info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        return false;
    }

    png_init_io(png, file);
    // png_set_compression_level(png, 6);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_alpha_mode(png, PNG_ALPHA_PREMULTIPLIED, PNG_GAMMA_LINEAR);
    png_set_bgr(png);
    png_write_info(png, info);

    png_bytep *row_ptrs = malloc(height * sizeof(*row_ptrs));
    if (!row_ptrs) {
        png_destroy_write_struct(&png, &info);
        return false;
    }

    for (int i = 0; i < height; ++i) {
        row_ptrs[i] = data + i * stride;
    }

    png_write_image(png, row_ptrs);
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    free(row_ptrs);

    return true;
}

bool image_write_to_file(struct painter_buffer *buffer, const char *filename)
{
    FILE *file = fopen(filename, "wb");
    if (!file) {
        kywc_log(KYWC_ERROR, "cannot open file: %s", filename);
        return false;
    }

    int width = buffer->base.width;
    int height = buffer->base.height;
    size_t stride = buffer->stride;
    uint8_t *data = buffer->data;
    bool ok = false;

    size_t len = strlen(filename);
    const char *suffix = filename + len - 3;
    if (len > 3 && strncmp(suffix, "bmp", 3) == 0) {
        ok = do_encode_bmp(file, width, height, stride, data);
    } else {
        ok = do_encode_png(file, width, height, stride, data);
    }

    fclose(file);
    return ok;
}
