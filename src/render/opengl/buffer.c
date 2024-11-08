// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>

#include <wlr/interfaces/wlr_buffer.h>

#include <kywc/log.h>

#include "render/opengl.h"
#include "render/pixel_format.h"
#include "render/renderer.h"

#ifndef EGL_WL_bind_wayland_display
#define EGL_WAYLAND_BUFFER_WL 0x31D5
#define EGL_WAYLAND_PLANE_WL 0x31D6
// #define EGL_TEXTURE_Y_U_V_WL 0x31D7
// #define EGL_TEXTURE_Y_UV_WL 0x31D8
// #define EGL_TEXTURE_Y_XUXV_WL 0x31D9
// #define EGL_TEXTURE_EXTERNAL_WL 0x31DA
// #define EGL_WAYLAND_Y_INVERTED_WL 0x31DB
#endif // EGL_WL_bind_wayland_display

typedef GLboolean (*PFNEGLBINDWAYLANDDISPLAYWL)(EGLDisplay dpy, struct wl_display *display);
typedef GLboolean (*PFNEGLUNBINDWAYLANDDISPLAYWL)(EGLDisplay dpy, struct wl_display *display);
typedef GLboolean (*PFNEGLQUERYWAYLANDBUFFERWL)(EGLDisplay dpy, struct wl_resource *buffer,
                                                EGLint attribute, EGLint *value);

struct ky_wayland_buffer {
    struct wl_list link;
    struct wlr_buffer base;
    struct wl_listener release;
    struct wl_resource *resource;
    struct wl_listener resource_destroy;

    EGLint width, height, format;
};

struct ky_wayland_buffer_manager {
    struct wl_list buffers;

    struct {
        PFNEGLBINDWAYLANDDISPLAYWL eglBindWaylandDisplayWL;
        PFNEGLUNBINDWAYLANDDISPLAYWL eglUnbindWaylandDisplayWL;
        PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;
    } procs;

    struct ky_opengl_renderer *renderer;
    struct wl_listener renderer_destroy;
    struct wl_display *display;
    struct wl_listener display_destroy;
};

static struct ky_wayland_buffer_manager *manager = NULL;

static const struct wlr_buffer_impl wayland_buffer_impl;

bool wlr_buffer_is_wayland_buffer(struct wlr_buffer *buffer)
{
    return buffer->impl == &wayland_buffer_impl;
}

static struct ky_wayland_buffer *wayland_buffer_from_buffer(struct wlr_buffer *wlr_buffer)
{
    assert(wlr_buffer->impl == &wayland_buffer_impl);
    struct ky_wayland_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
    return buffer;
}

static void wayland_buffer_destroy(struct ky_wayland_buffer *buffer)
{
    wl_list_remove(&buffer->resource_destroy.link);
    wl_list_remove(&buffer->release.link);
    wl_list_remove(&buffer->link);
    free(buffer);
}

static void buffer_destroy(struct wlr_buffer *wlr_buffer)
{
    struct ky_wayland_buffer *buffer = wayland_buffer_from_buffer(wlr_buffer);
    wayland_buffer_destroy(buffer);
}

// TODO: return format for buffer_is_opaque
static const struct wlr_buffer_impl wayland_buffer_impl = {
    .destroy = buffer_destroy,
};

static void wayland_buffer_handle_resource_destroy(struct wl_listener *listener, void *data)
{
    struct ky_wayland_buffer *buffer = wl_container_of(listener, buffer, resource_destroy);
    buffer->resource = NULL;
    wl_list_remove(&buffer->resource_destroy.link);
    wl_list_init(&buffer->resource_destroy.link);
    wlr_buffer_drop(&buffer->base);
}

static void wayland_buffer_handle_release(struct wl_listener *listener, void *data)
{
    struct ky_wayland_buffer *buffer = wl_container_of(listener, buffer, release);
    if (buffer->resource != NULL) {
        wl_buffer_send_release(buffer->resource);
    }
}

static struct ky_wayland_buffer *wayland_buffer_get_or_create(struct wl_resource *resource)
{
    struct ky_wayland_buffer *buffer = NULL;
    wl_list_for_each(buffer, &manager->buffers, link) {
        if (buffer->resource == resource) {
            return buffer;
        }
    }

    buffer = calloc(1, sizeof(*buffer));
    if (buffer == NULL) {
        return NULL;
    }

    EGLDisplay display = manager->renderer->egl->display;
    manager->procs.eglQueryWaylandBufferWL(display, resource, EGL_WIDTH, &buffer->width);
    manager->procs.eglQueryWaylandBufferWL(display, resource, EGL_HEIGHT, &buffer->height);
    manager->procs.eglQueryWaylandBufferWL(display, resource, EGL_TEXTURE_FORMAT, &buffer->format);

    wlr_buffer_init(&buffer->base, &wayland_buffer_impl, buffer->width, buffer->height);
    buffer->release.notify = wayland_buffer_handle_release;
    wl_signal_add(&buffer->base.events.release, &buffer->release);

    buffer->resource = resource;
    buffer->resource_destroy.notify = wayland_buffer_handle_resource_destroy;
    wl_resource_add_destroy_listener(resource, &buffer->resource_destroy);

    wl_list_insert(&manager->buffers, &buffer->link);

    return buffer;
}

static struct wlr_buffer *wayland_buffer_from_resource(struct wl_resource *resource)
{
    struct ky_wayland_buffer *buffer = wayland_buffer_get_or_create(resource);
    return buffer ? &buffer->base : NULL;
}

static bool wayland_buffer_is_instance(struct wl_resource *resource)
{
    EGLint format;
    return manager->procs.eglQueryWaylandBufferWL(manager->renderer->egl->display, resource,
                                                  EGL_TEXTURE_FORMAT, &format);
}

static const struct wlr_buffer_resource_interface wayland_buffer_interface = {
    .name = "wayland_buffer",
    .is_instance = wayland_buffer_is_instance,
    .from_resource = wayland_buffer_from_resource
};

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->display_destroy.link);
    manager->procs.eglUnbindWaylandDisplayWL(manager->renderer->egl->display, manager->display);
    manager->display = NULL;
}

static void handle_renderer_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->renderer_destroy.link);

    struct ky_wayland_buffer *buffer, *tmp;
    wl_list_for_each_safe(buffer, tmp, &manager->buffers, link) {
        wayland_buffer_destroy(buffer);
    }

    free(manager);
    manager = NULL;
}

bool ky_wayland_buffer_create(struct wl_display *wl_display, struct wlr_renderer *wlr_renderer)
{
    if (!wlr_renderer_is_opengl(wlr_renderer)) {
        return false;
    }

    struct ky_opengl_renderer *renderer = ky_opengl_renderer_from_wlr_renderer(wlr_renderer);
    if (!renderer->egl->exts.WL_bind_wayland_display) {
        return false;
    }

    manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    manager->procs.eglBindWaylandDisplayWL = (void *)eglGetProcAddress("eglBindWaylandDisplayWL");
    manager->procs.eglUnbindWaylandDisplayWL =
        (void *)eglGetProcAddress("eglUnbindWaylandDisplayWL");
    manager->procs.eglQueryWaylandBufferWL = (void *)eglGetProcAddress("eglQueryWaylandBufferWL");

    if (!manager->procs.eglBindWaylandDisplayWL(renderer->egl->display, wl_display)) {
        free(manager);
        manager = NULL;
        return false;
    }

    kywc_log(KYWC_INFO, "egl bind wayland display");

    wl_list_init(&manager->buffers);
    manager->renderer = renderer;
    manager->renderer_destroy.notify = handle_renderer_destroy;
    wl_signal_add(&wlr_renderer->events.destroy, &manager->renderer_destroy);

    manager->display = wl_display;
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(wl_display, &manager->display_destroy);

    wlr_buffer_register_resource_interface(&wayland_buffer_interface);

    return true;
}

static struct wlr_texture *gl_texture_from_wayland(struct wlr_renderer *wlr_renderer,
                                                   struct wlr_buffer *wlr_buffer)
{
    struct ky_opengl_renderer *renderer = ky_opengl_renderer_from_wlr_renderer(wlr_renderer);
    if (!renderer->exts.OES_egl_image) {
        return NULL;
    }

    struct ky_wayland_buffer *buffer = wayland_buffer_from_buffer(wlr_buffer);
    struct ky_opengl_texture *texture =
        ky_opengl_texture_create(renderer, buffer->width, buffer->height);
    if (texture == NULL) {
        return NULL;
    }
    texture->drm_format = DRM_FORMAT_INVALID; // texture can't be written anyways

    const struct ky_pixel_format *fmt = ky_pixel_format_from_drm(buffer->format);
    if (fmt != NULL) {
        texture->has_alpha = fmt->has_alpha;
    } else {
        // We don't know, assume the texture has an alpha channel
        texture->has_alpha = true;
    }

    struct ky_egl_context prev_ctx;
    ky_egl_make_current(renderer->egl, &prev_ctx);

    const EGLint attribs[] = {
        EGL_WAYLAND_PLANE_WL,
        0,
        EGL_NONE,
    };
    texture->image =
        eglCreateImageKHR(renderer->egl->display, EGL_NO_CONTEXT, EGL_WAYLAND_BUFFER_WL,
                          (EGLClientBuffer)buffer->resource, attribs);
    if (texture->image == EGL_NO_IMAGE_KHR) {
        kywc_log(KYWC_ERROR, "Failed to create EGL image from wayland buffer");
        ky_egl_restore_context(&prev_ctx);
        wl_list_remove(&texture->link);
        free(texture);
        return NULL;
    }

    texture->target = GL_TEXTURE_2D;

    ky_opengl_push_debug(renderer);

    glGenTextures(1, &texture->tex);
    glBindTexture(texture->target, texture->tex);
    glTexParameteri(texture->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(texture->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glEGLImageTargetTexture2DOES(texture->target, texture->image);
    glBindTexture(texture->target, 0);

    ky_opengl_pop_debug(renderer);

    ky_egl_restore_context(&prev_ctx);

    return &texture->wlr_texture;
}

static void texture_handle_buffer_destroy(struct wlr_addon *addon)
{
    struct ky_opengl_texture *texture = wl_container_of(addon, texture, buffer_addon);
    ky_opengl_texture_destroy(texture);
}

static const struct wlr_addon_interface texture_addon_impl = {
    .name = "ky_wayland_texture",
    .destroy = texture_handle_buffer_destroy,
};

struct wlr_texture *wlr_texture_from_wayland_buffer(struct ky_opengl_renderer *renderer,
                                                    struct wlr_buffer *buffer)
{
    struct wlr_addon *addon = wlr_addon_find(&buffer->addons, renderer, &texture_addon_impl);
    if (addon != NULL) {
        struct ky_opengl_texture *texture = wl_container_of(addon, texture, buffer_addon);
        if (!ky_opengl_texture_invalidate(texture)) {
            kywc_log(KYWC_ERROR, "Failed to invalidate texture");
            return NULL;
        }
        wlr_buffer_lock(texture->buffer);
        return &texture->wlr_texture;
    }

    struct wlr_texture *wlr_texture = gl_texture_from_wayland(&renderer->wlr_renderer, buffer);
    if (wlr_texture == NULL) {
        return NULL;
    }

    struct ky_opengl_texture *texture = ky_opengl_texture_from_wlr_texture(wlr_texture);
    texture->buffer = wlr_buffer_lock(buffer);
    wlr_addon_init(&texture->buffer_addon, &buffer->addons, renderer, &texture_addon_impl);

    return &texture->wlr_texture;
}
