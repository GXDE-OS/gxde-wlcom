// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _RENDER_PIXEL_FORMAT_H_
#define _RENDER_PIXEL_FORMAT_H_

#include "render/opengl.h"

struct ky_pixel_format {
    uint32_t drm_format;
    /* Bytes per block (including padding) */
    uint32_t bytes_per_block;
    /* Size of a block in pixels (zero for 1×1) */
    uint32_t block_width, block_height;
    /* Support alpha channel */
    bool has_alpha;

    // optional field, if empty then internalformat = format
    GLint gl_internalformat;
    GLint gl_format, gl_type;
};

typedef bool (*ky_pixel_formats_iterator_func_t)(const struct ky_pixel_format *format, void *data);

void ky_pixel_formats_for_each(ky_pixel_formats_iterator_func_t iterator, void *data);

const struct ky_pixel_format *ky_pixel_format_from_drm(uint32_t fmt);

const struct ky_pixel_format *ky_pixel_format_from_gl(GLint gl_format, GLint gl_type, bool alpha);

uint32_t ky_pixel_format_pixels_per_block(const struct ky_pixel_format *format);

bool ky_pixel_format_check_stride(const struct ky_pixel_format *format, int32_t stride,
                                  int32_t width);

int32_t ky_pixel_format_min_stride(const struct ky_pixel_format *format, int32_t width);

/* opengl stuff */
bool ky_opengl_pixel_format_is_supported(const struct ky_opengl_renderer *renderer,
                                         const struct ky_pixel_format *format);

const uint32_t *ky_opengl_get_shm_formats(const struct ky_opengl_renderer *renderer, size_t *len);

#endif /* _RENDER_PIXEL_FORMAT_H_ */
