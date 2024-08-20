// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>

#include <drm_fourcc.h>
#include <gbm.h>

#include <wlr/backend.h>
#include <wlr/config.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pixman.h>
#include <wlr/render/vulkan.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>

#include <kywc/log.h>

#include "render/opengl.h"
#include "render/renderer.h"
#include "renderer_p.h"

static struct single_plane_formats {
    struct wlr_drm_format_set formats;
    struct wl_listener destroy;
    int (*get_format_modifier_plane_count)(struct gbm_device *gbm, uint32_t format,
                                           uint64_t modifier);
} *formats = NULL;

static void handle_renderer_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&formats->destroy.link);
    wlr_drm_format_set_finish(&formats->formats);
    free(formats);
}

static void query_single_plane_formats(struct wlr_renderer *renderer)
{
    if (wlr_renderer_is_opengl(renderer)) {
        struct ky_opengl_renderer *r = ky_opengl_renderer_from_wlr_renderer(renderer);
        if (!r->egl->has_modifiers) {
            return;
        }
    }

    formats = calloc(1, sizeof(*formats));
    if (!formats) {
        return;
    }

    int drm_fd = wlr_renderer_get_drm_fd(renderer);
    if (drm_fd < 0) {
        goto out;
    }
    struct gbm_device *gbm_device = gbm_create_device(drm_fd);
    if (!gbm_device) {
        goto out;
    }

    const struct wlr_drm_format_set *render_formats = renderer->impl->get_render_formats(renderer);
    if (!render_formats) {
        gbm_device_destroy(gbm_device);
        goto out;
    }

    formats->get_format_modifier_plane_count =
        dlsym(RTLD_DEFAULT, "gbm_device_get_format_modifier_plane_count");
    if (formats->get_format_modifier_plane_count == NULL) {
        kywc_log(KYWC_WARN, "No gbm_device_get_format_modifier_plane_count support!");
    }

    const struct wlr_drm_format *fmt;
    for (size_t i = 0; i < render_formats->len; i++) {
        fmt = &render_formats->formats[i];
        for (size_t j = 0; j < fmt->len; ++j) {
            if (fmt->modifiers[j] == DRM_FORMAT_MOD_INVALID ||
                fmt->modifiers[j] == DRM_FORMAT_MOD_LINEAR) {
                wlr_drm_format_set_add(&formats->formats, fmt->format, fmt->modifiers[j]);
                continue;
            }
            if (formats->get_format_modifier_plane_count &&
                formats->get_format_modifier_plane_count(gbm_device, fmt->format,
                                                         fmt->modifiers[j]) != 1) {
                continue;
            }
            wlr_drm_format_set_add(&formats->formats, fmt->format, fmt->modifiers[j]);
        }
    }

    if (formats->formats.len == 0) {
        wlr_drm_format_set_finish(&formats->formats);
        gbm_device_destroy(gbm_device);
        goto out;
    }

    formats->destroy.notify = handle_renderer_destroy;
    wl_signal_add(&renderer->events.destroy, &formats->destroy);

    gbm_device_destroy(gbm_device);
    return;

out:
    free(formats);
    formats = NULL;
}

struct wlr_renderer *ky_renderer_autocreate(struct wlr_backend *backend)
{
    const char *api = getenv("KYWC_RENDERER");
    if (api && strcmp(api, "pixman") == 0) {
        return wlr_pixman_renderer_create();
    }

    struct wlr_renderer *renderer = NULL;
    /* get drm fd from backend */
    int drm_fd = wlr_backend_get_drm_fd(backend);
    if (drm_fd < 0) {
        kywc_log(KYWC_ERROR, "Cannot create hardware renderer: no DRM fd available");
    } else {
        if (api && strcmp(api, "vulkan") == 0) {
#if WLR_HAS_VULKAN_RENDERER
            renderer = wlr_vk_renderer_create_with_drm_fd(drm_fd);
#else
            kywc_log(KYWC_ERROR, "Cannot create Vulkan renderer: wlroots not compiled with Vulkan");
#endif
        } else {
            renderer = ky_opengl_renderer_create_with_drm_fd(drm_fd);
        }
    }

    if (renderer) {
        query_single_plane_formats(renderer);
    } else {
        kywc_log(KYWC_WARN, "Failed to create a hardware renderer");
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
                                                           uint32_t fmt, bool single_plane)
{
    if (!renderer->impl->get_render_formats) {
        return NULL;
    }

    const struct wlr_drm_format_set *render_formats =
        (formats && single_plane) ? &formats->formats
                                  : renderer->impl->get_render_formats(renderer);
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
                                             uint32_t fmt, bool single_plane)
{
    if (wlr_renderer_is_pixman(renderer)) {
        return shm_create_buffer(width, height, fmt);
    }

    const struct wlr_drm_format *format =
        ky_renderer_get_render_format(renderer, fmt, single_plane);
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

    /* return true if software opengl */
    if (wlr_renderer_is_opengl(renderer)) {
        struct ky_opengl_renderer *r = ky_opengl_renderer_from_wlr_renderer(renderer);
        return r->egl->is_software;
    }

    return false;
}
