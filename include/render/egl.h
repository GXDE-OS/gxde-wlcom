// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _RENDER_EGL_H_
#define _RENDER_EGL_H_

#include <epoxy/egl.h>

#include <wlr/render/dmabuf.h>
#include <wlr/render/drm_format_set.h>

struct ky_egl {
    EGLDisplay display;
    EGLContext context;
    EGLDeviceEXT device; // may be EGL_NO_DEVICE_EXT
    struct gbm_device *gbm_device;

    // opengl or opengles
    bool is_gles;

    // https://registry.khronos.org/EGL/extensions/
    struct {
        // Display extensions
        bool KHR_image_base;
        bool EXT_image_dma_buf_import;
        bool EXT_image_dma_buf_import_modifiers;
        bool IMG_context_priority;
        bool EXT_create_context_robustness;
        bool WL_bind_wayland_display;

        // Device extensions
        bool EXT_device_drm;
        bool EXT_device_drm_render_node;

        // Client extensions
        bool KHR_platform_gbm;
        bool EXT_platform_device;
        bool KHR_display_reference;
        bool EXT_device_enumeration;
        bool EXT_device_query;
    } exts;

    bool has_modifiers;
    struct wlr_drm_format_set dmabuf_texture_formats;
    struct wlr_drm_format_set dmabuf_render_formats;
    struct wlr_drm_format_set dmabuf_render_single_plane_formats;
};

struct ky_egl_context {
    EGLDisplay display;
    EGLContext context;
    EGLSurface draw_surface;
    EGLSurface read_surface;
};

struct ky_egl *ky_egl_create_with_drm_fd(int drm_fd);

void ky_egl_destroy(struct ky_egl *egl);

bool ky_egl_make_current(struct ky_egl *egl);

bool ky_egl_unset_current(struct ky_egl *egl);

bool ky_egl_is_current(struct ky_egl *egl);

void ky_egl_save_context(struct ky_egl_context *context);

bool ky_egl_restore_context(struct ky_egl_context *context);

bool ky_egl_destroy_image(struct ky_egl *egl, EGLImageKHR image);

EGLImageKHR ky_egl_create_image_from_dmabuf(struct ky_egl *egl,
                                            struct wlr_dmabuf_attributes *attributes,
                                            bool *external_only);

int ky_egl_dup_drm_fd(struct ky_egl *egl);

#endif /* _RENDER_EGL_H_ */
