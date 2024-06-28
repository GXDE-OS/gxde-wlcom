// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pixman.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>

#include <kywc/log.h>

#include "render/opengl.h"
#include "render/renderer.h"
#include "renderer_p.h"

struct wlr_renderer *ky_renderer_autocreate(struct wlr_backend *backend)
{
    struct wlr_renderer *renderer = NULL;

    /* get drm fd from backend */
    int drm_fd = wlr_backend_get_drm_fd(backend);
    if (drm_fd < 0) {
        kywc_log(KYWC_ERROR, "Cannot create OpenGL renderer: no DRM fd available");
    } else {
        /* try opengl renderer first */
        renderer = ky_opengl_renderer_create_with_drm_fd(drm_fd);
    }

    if (!renderer) {
        kywc_log(KYWC_ERROR, "Failed to create a OpenGL renderer");
        renderer = wlr_pixman_renderer_create();
    }

    if (!renderer) {
        kywc_log(KYWC_ERROR, "Failed to create a pixman renderer");
        kywc_log(KYWC_ERROR, "Could not initialize renderer");
    }

    return renderer;
}

bool ky_renderer_init_wl_display(struct wlr_renderer *renderer, struct wlr_backend *backend,
                                 struct wl_display *wl_display)
{
    if (!wlr_renderer_init_wl_shm(renderer, wl_display)) {
        return false;
    }

    if (!wlr_renderer_get_dmabuf_texture_formats(renderer)) {
        return true;
    }

    if (!wlr_linux_dmabuf_v1_create_with_renderer(wl_display, 4, renderer)) {
        return false;
    }

    if (!ky_wayland_buffer_create(wl_display, renderer)) {
        /* create wl_drm if not created in driver */
        int master_fd = wlr_backend_get_drm_fd(backend);
        if (master_fd >= 0) {
            wayland_drm_create(wl_display, renderer, master_fd);
        } else {
            kywc_log(KYWC_WARN, "Cannot get renderer DRM FD, disabling wl_drm");
        }
    }

    return true;
}

const struct wlr_drm_format *ky_renderer_get_render_format(struct wlr_renderer *renderer,
                                                           uint32_t fmt)
{
    if (!renderer->impl->get_render_formats) {
        return NULL;
    }

    const struct wlr_drm_format_set *render_formats = NULL;
    if (wlr_renderer_is_opengl(renderer)) {
        struct ky_opengl_renderer *r = ky_opengl_renderer_from_wlr_renderer(renderer);
        render_formats = &r->egl->dmabuf_render_single_plane_formats;
    } else {
        render_formats = renderer->impl->get_render_formats(renderer);
    }

    if (!render_formats) {
        kywc_log(KYWC_ERROR, "Failed to get render formats");
        return NULL;
    }

    const struct wlr_drm_format *render_format = wlr_drm_format_set_get(render_formats, fmt);
    if (!render_format) {
        kywc_log(KYWC_ERROR, "Renderer doesn't support format 0x%" PRIX32, fmt);
        return NULL;
    }

    return render_format;
}

struct wlr_buffer *ky_renderer_create_buffer(struct wlr_renderer *renderer,
                                             struct wlr_allocator *alloc, int width, int height,
                                             uint32_t fmt)
{
    if (wlr_renderer_is_pixman(renderer)) {
        return shm_create_buffer(width, height, fmt);
    }

    const struct wlr_drm_format *format = ky_renderer_get_render_format(renderer, fmt);
    if (!format) {
        return NULL;
    }
    return wlr_allocator_create_buffer(alloc, width, height, format);
}

bool ky_renderer_is_software(struct wlr_renderer *renderer)
{
    if (wlr_renderer_is_pixman(renderer)) {
        return true;
    }

    /* return true if software opengl or vulkan */
    if (wlr_renderer_is_opengl(renderer)) {
        struct ky_opengl_renderer *r = ky_opengl_renderer_from_wlr_renderer(renderer);
        return r->egl->is_software;
    }

    return false;
}
