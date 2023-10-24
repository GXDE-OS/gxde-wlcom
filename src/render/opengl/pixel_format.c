// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <drm_fourcc.h>

#include <kywc/log.h>

#include "render/opengl.h"

/*
 * The DRM formats are little endian while the GL formats are big endian,
 * so DRM_FORMAT_ARGB8888 is actually compatible with GL_BGRA_EXT.
 */
static const struct ky_opengl_pixel_format formats[] = {
    {
        .drm_format = DRM_FORMAT_ARGB8888,
        .opaque_substitute = DRM_FORMAT_XRGB8888,
        .bytes_per_block = 4,
        .gl_format = GL_BGRA_EXT,
        .gl_type = GL_UNSIGNED_BYTE,
        .has_alpha = true,
    },
    {
        .drm_format = DRM_FORMAT_XRGB8888,
        .bytes_per_block = 4,
        .gl_format = GL_BGRA_EXT,
        .gl_type = GL_UNSIGNED_BYTE,
    },
    {
        .drm_format = DRM_FORMAT_XBGR8888,
        .bytes_per_block = 4,
        .gl_format = GL_RGBA,
        .gl_type = GL_UNSIGNED_BYTE,
    },
    {
        .drm_format = DRM_FORMAT_ABGR8888,
        .opaque_substitute = DRM_FORMAT_XBGR8888,
        .bytes_per_block = 4,
        .gl_format = GL_RGBA,
        .gl_type = GL_UNSIGNED_BYTE,
        .has_alpha = true,
    },
    {
        .drm_format = DRM_FORMAT_RGBX8888,
        .bytes_per_block = 4,
        .gl_format = GL_ABGR_EXT,
        .gl_type = GL_UNSIGNED_BYTE,
    },
    {
        .drm_format = DRM_FORMAT_RGBA8888,
        .opaque_substitute = DRM_FORMAT_RGBX8888,
        .bytes_per_block = 4,
        .gl_format = GL_ABGR_EXT,
        .gl_type = GL_UNSIGNED_BYTE,
        .has_alpha = true,
    },
    {
        .drm_format = DRM_FORMAT_BGRX8888,
        .bytes_per_block = 4,
        .gl_format = GL_ABGR_EXT,
        .gl_type = GL_UNSIGNED_BYTE,
    },
    {
        .drm_format = DRM_FORMAT_BGRA8888,
        .opaque_substitute = DRM_FORMAT_BGRX8888,
        .bytes_per_block = 4,
        .gl_format = GL_ABGR_EXT,
        .gl_type = GL_UNSIGNED_BYTE,
        .has_alpha = true,
    },
    {
        .drm_format = DRM_FORMAT_R8,
        .bytes_per_block = 1,
        .gl_format = GL_R,
        .gl_type = GL_UNSIGNED_BYTE,
    },
    {
        .drm_format = DRM_FORMAT_GR88,
        .bytes_per_block = 2,
        .gl_format = GL_RG,
        .gl_type = GL_UNSIGNED_BYTE,
    },
    {
        .drm_format = DRM_FORMAT_RGB888,
        .bytes_per_block = 3,
        .gl_format = GL_BGR,
        .gl_type = GL_UNSIGNED_BYTE,
    },
    {
        .drm_format = DRM_FORMAT_BGR888,
        .bytes_per_block = 3,
        .gl_format = GL_RGB,
        .gl_type = GL_UNSIGNED_BYTE,
    },
    /* little endian */
    {
        .drm_format = DRM_FORMAT_RGBX4444,
        .bytes_per_block = 2,
        .gl_format = GL_RGBA,
        .gl_type = GL_UNSIGNED_SHORT_4_4_4_4,
    },
    {
        .drm_format = DRM_FORMAT_RGBA4444,
        .opaque_substitute = DRM_FORMAT_RGBX4444,
        .bytes_per_block = 2,
        .gl_format = GL_RGBA,
        .gl_type = GL_UNSIGNED_SHORT_4_4_4_4,
        .has_alpha = true,
    },
    {
        .drm_format = DRM_FORMAT_BGRX4444,
        .bytes_per_block = 2,
        .gl_format = GL_BGRA_EXT,
        .gl_type = GL_UNSIGNED_SHORT_4_4_4_4,
    },
    {
        .drm_format = DRM_FORMAT_BGRA4444,
        .opaque_substitute = DRM_FORMAT_BGRX4444,
        .bytes_per_block = 2,
        .gl_format = GL_BGRA_EXT,
        .gl_type = GL_UNSIGNED_SHORT_4_4_4_4,
        .has_alpha = true,
    },
    {
        .drm_format = DRM_FORMAT_RGBX5551,
        .bytes_per_block = 2,
        .gl_format = GL_RGBA,
        .gl_type = GL_UNSIGNED_SHORT_5_5_5_1,
    },
    {
        .drm_format = DRM_FORMAT_RGBA5551,
        .opaque_substitute = DRM_FORMAT_RGBX5551,
        .bytes_per_block = 2,
        .gl_format = GL_RGBA,
        .gl_type = GL_UNSIGNED_SHORT_5_5_5_1,
        .has_alpha = true,
    },
    {
        .drm_format = DRM_FORMAT_BGRX5551,
        .bytes_per_block = 2,
        .gl_format = GL_BGRA_EXT,
        .gl_type = GL_UNSIGNED_SHORT_5_5_5_1,
    },
    {
        .drm_format = DRM_FORMAT_BGRA5551,
        .opaque_substitute = DRM_FORMAT_BGRX5551,
        .bytes_per_block = 2,
        .gl_format = GL_BGRA_EXT,
        .gl_type = GL_UNSIGNED_SHORT_5_5_5_1,
        .has_alpha = true,
    },
    {
        .drm_format = DRM_FORMAT_XRGB1555,
        .bytes_per_block = 2,
        .gl_format = GL_BGRA_EXT,
        .gl_type = GL_UNSIGNED_SHORT_1_5_5_5_REV,
    },
    {
        .drm_format = DRM_FORMAT_ARGB1555,
        .opaque_substitute = DRM_FORMAT_XRGB1555,
        .bytes_per_block = 2,
        .gl_format = GL_BGRA_EXT,
        .gl_type = GL_UNSIGNED_SHORT_1_5_5_5_REV,
        .has_alpha = true,
    },
    {
        .drm_format = DRM_FORMAT_RGB565,
        .bytes_per_block = 2,
        .gl_format = GL_RGB,
        .gl_type = GL_UNSIGNED_SHORT_5_6_5,
    },
    {
        .drm_format = DRM_FORMAT_BGR565,
        .bytes_per_block = 2,
        .gl_format = GL_BGR,
        .gl_type = GL_UNSIGNED_SHORT_5_6_5,
    },
    {
        .drm_format = DRM_FORMAT_XRGB2101010,
        .bytes_per_block = 4,
        .gl_format = GL_BGRA_EXT,
        .gl_type = GL_UNSIGNED_INT_2_10_10_10_REV_EXT,

    },
    {
        .drm_format = DRM_FORMAT_ARGB2101010,
        .opaque_substitute = DRM_FORMAT_XRGB2101010,
        .bytes_per_block = 4,
        .gl_format = GL_BGRA_EXT,
        .gl_type = GL_UNSIGNED_INT_2_10_10_10_REV_EXT,
        .has_alpha = true,
    },
    {
        .drm_format = DRM_FORMAT_XBGR2101010,
        .bytes_per_block = 4,
        .gl_format = GL_RGBA,
        .gl_type = GL_UNSIGNED_INT_2_10_10_10_REV_EXT,
    },
    {
        .drm_format = DRM_FORMAT_ABGR2101010,
        .opaque_substitute = DRM_FORMAT_XBGR2101010,
        .bytes_per_block = 4,
        .gl_format = GL_RGBA,
        .gl_type = GL_UNSIGNED_INT_2_10_10_10_REV_EXT,
        .has_alpha = true,
    },
    {
        .drm_format = DRM_FORMAT_XBGR16161616F,
        .bytes_per_block = 8,
        .gl_format = GL_RGBA,
        .gl_type = GL_HALF_FLOAT_OES,
    },
    {
        .drm_format = DRM_FORMAT_ABGR16161616F,
        .opaque_substitute = DRM_FORMAT_XBGR16161616F,
        .bytes_per_block = 8,
        .gl_format = GL_RGBA,
        .gl_type = GL_HALF_FLOAT_OES,
        .has_alpha = true,
    },
    {
        .drm_format = DRM_FORMAT_XBGR16161616,
        .bytes_per_block = 8,
        .gl_internalformat = GL_RGBA16_EXT,
        .gl_format = GL_RGBA,
        .gl_type = GL_UNSIGNED_SHORT,
    },
    {
        .drm_format = DRM_FORMAT_ABGR16161616,
        .opaque_substitute = DRM_FORMAT_XBGR16161616,
        .bytes_per_block = 8,
        .gl_internalformat = GL_RGBA16_EXT,
        .gl_format = GL_RGBA,
        .gl_type = GL_UNSIGNED_SHORT,
        .has_alpha = true,
    },
    {
        .drm_format = DRM_FORMAT_YVYU,
        .bytes_per_block = 4,
        .block_width = 2,
        .block_height = 1,
    },
    {
        .drm_format = DRM_FORMAT_VYUY,
        .bytes_per_block = 4,
        .block_width = 2,
        .block_height = 1,
    },
};

/*
 * Return true if supported for texturing, even if other operations like
 * reading aren't supported.
 */
bool ky_opengl_pixel_format_is_supported(const struct ky_opengl_renderer *renderer,
                                         const struct ky_opengl_pixel_format *format)
{
    if (format->gl_type == 0) {
        return false;
    }
    if (format->gl_type == GL_UNSIGNED_INT_2_10_10_10_REV_EXT &&
        !renderer->exts.EXT_texture_type_2_10_10_10_REV) {
        return false;
    }
    if (format->gl_type == GL_HALF_FLOAT_OES && !renderer->exts.OES_texture_half_float_linear) {
        return false;
    }
    if (format->gl_type == GL_UNSIGNED_SHORT && !renderer->exts.EXT_texture_norm16) {
        return false;
    }
    /*
     * Note that we don't need to check for GL_EXT_texture_format_BGRA8888
     * here, since we've already checked if we have it at renderer creation
     * time and bailed out if not. We do the check there because Wayland
     * requires all compositors to support SHM buffers in that format.
     */
    return true;
}

const struct ky_opengl_pixel_format *ky_opengl_pixel_format_from_drm(uint32_t fmt)
{
    for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
        if (formats[i].drm_format == fmt) {
            return &formats[i];
        }
    }
    return NULL;
}

const struct ky_opengl_pixel_format *ky_opengl_pixel_format_from_gl(GLint gl_format, GLint gl_type,
                                                                    bool alpha)
{
    for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
        if (formats[i].gl_format == gl_format && formats[i].gl_type == gl_type &&
            formats[i].has_alpha == alpha) {
            return &formats[i];
        }
    }
    return NULL;
}

const uint32_t *ky_opengl_get_shm_formats(const struct ky_opengl_renderer *renderer, size_t *len)
{
    static uint32_t shm_formats[sizeof(formats) / sizeof(formats[0])];
    size_t j = 0;
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        if (!ky_opengl_pixel_format_is_supported(renderer, &formats[i])) {
            continue;
        }
        shm_formats[j++] = formats[i].drm_format;
    }
    *len = j;
    return shm_formats;
}

uint32_t ky_opengl_pixel_format_pixels_per_block(const struct ky_opengl_pixel_format *format)
{
    uint32_t pixels = format->block_width * format->block_height;
    return pixels > 0 ? pixels : 1;
}

static int32_t div_round_up(int32_t dividend, int32_t divisor)
{
    int32_t quotient = dividend / divisor;
    if (dividend % divisor != 0) {
        quotient++;
    }
    return quotient;
}

int32_t ky_opengl_pixel_format_min_stride(const struct ky_opengl_pixel_format *format,
                                          int32_t width)
{
    int32_t pixels_per_block = (int32_t)ky_opengl_pixel_format_pixels_per_block(format);
    int32_t bytes_per_block = (int32_t)format->bytes_per_block;
    if (width > INT32_MAX / bytes_per_block) {
        kywc_log(KYWC_DEBUG, "Invalid width %d (overflow)", width);
        return 0;
    }
    return div_round_up(width * bytes_per_block, pixels_per_block);
}

bool ky_opengl_pixel_format_check_stride(const struct ky_opengl_pixel_format *format,
                                         int32_t stride, int32_t width)
{
    int32_t bytes_per_block = (int32_t)format->bytes_per_block;
    if (stride % bytes_per_block != 0) {
        kywc_log(KYWC_DEBUG, "Invalid stride %d (incompatible with %d bytes-per-block)", stride,
                 bytes_per_block);
        return false;
    }

    int32_t min_stride = ky_opengl_pixel_format_min_stride(format, width);
    if (min_stride <= 0) {
        return false;
    } else if (stride < min_stride) {
        kywc_log(KYWC_DEBUG, "Invalid stride %d (too small for %d bytes-per-block and width %d)",
                 stride, bytes_per_block, width);
        return false;
    }

    return true;
}
