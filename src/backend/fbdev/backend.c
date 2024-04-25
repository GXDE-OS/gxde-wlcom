// SPDX-FileCopyrightText: 2024 The wlroots contributors
// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <assert.h>
#include <stdlib.h>

#include <kywc/log.h>

#include <wlr/backend/interface.h>
#include <wlr/backend/session.h>

#include "fbdev_p.h"

struct fbdev_backend *get_fbdev_backend_from_backend(struct wlr_backend *wlr_backend)
{
    assert(wlr_backend_is_fbdev(wlr_backend));
    struct fbdev_backend *backend = wl_container_of(wlr_backend, backend, backend);
    return backend;
}

static uint32_t get_buffer_caps(struct wlr_backend *wlr_backend)
{
    return WLR_BUFFER_CAP_DATA_PTR | WLR_BUFFER_CAP_SHM;
}

static bool backend_start(struct wlr_backend *wlr_backend)
{
    struct fbdev_backend *backend = get_fbdev_backend_from_backend(wlr_backend);
    struct fbdev_output *output;
    wl_list_for_each(output, &backend->outputs, link) {
        wl_signal_emit_mutable(&backend->backend.events.new_output, &output->wlr_output);
    }
    return true;
}

static void backend_destroy(struct wlr_backend *wlr_backend)
{
    if (!wlr_backend) {
        return;
    }
    struct fbdev_backend *backend = get_fbdev_backend_from_backend(wlr_backend);
    if (!backend) {
        return;
    }

    wlr_backend_finish(wlr_backend);

    struct fbdev_output *output, *output_tmp;
    wl_list_for_each_safe(output, output_tmp, &backend->outputs, link) {
        wlr_output_destroy(&output->wlr_output);
    }

    wl_list_remove(&backend->display_destroy.link);
    wl_list_remove(&backend->session_destroy.link);
    wl_list_remove(&backend->session_active.link);

    free(backend);
}

static const struct wlr_backend_impl backend_impl = {
    .start = backend_start,
    .destroy = backend_destroy,
    .get_buffer_caps = get_buffer_caps,
};

static void fbdev_output_damage_whole(struct fbdev_output *output)
{
    int width, height;
    struct wlr_output *wlr_output = &output->wlr_output;
    wlr_output_transformed_resolution(wlr_output, &width, &height);

    pixman_region32_t damage;
    pixman_region32_init_rect(&damage, 0, 0, width, height);

    struct wlr_output_event_damage event = {
        .output = wlr_output,
        .damage = &damage,
    };
    wl_signal_emit_mutable(&wlr_output->events.damage, &event);

    pixman_region32_fini(&damage);
}

static void handle_session_active(struct wl_listener *listener, void *data)
{
    struct fbdev_backend *backend = wl_container_of(listener, backend, session_active);
    struct wlr_session *session = backend->session;

    struct fbdev_output *output;
    wl_list_for_each(output, &backend->outputs, link) {
        if (session->active) {
            fbdev_output_reenable(output);
            fbdev_output_damage_whole(output);
        } else {
            fbdev_output_offscreen(output);
        }
    }
}

static void handle_session_destroy(struct wl_listener *listener, void *data)
{
    struct fbdev_backend *backend = wl_container_of(listener, backend, session_destroy);
    backend_destroy(&backend->backend);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct fbdev_backend *backend = wl_container_of(listener, backend, display_destroy);
    backend_destroy(&backend->backend);
}

bool wlr_backend_is_fbdev(struct wlr_backend *backend)
{
    return backend->impl == &backend_impl;
}

struct wlr_backend *fbdev_backend_create(struct wl_display *display, struct wlr_session *session,
                                         const char *device)
{
    struct fbdev_backend *backend = calloc(1, sizeof(*backend));
    if (!backend) {
        kywc_log(KYWC_ERROR, "failed to allocate fbdev_backend");
        return NULL;
    }

    wlr_backend_init(&backend->backend, &backend_impl);

    backend->display = display;
    backend->session = session;
    wl_list_init(&backend->outputs);

    fbdev_output_create(&backend->backend, device);

    backend->session_active.notify = handle_session_active;
    wl_signal_add(&session->events.active, &backend->session_active);
    backend->session_destroy.notify = handle_session_destroy;
    wl_signal_add(&session->events.destroy, &backend->session_destroy);
    backend->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(display, &backend->display_destroy);

    return &backend->backend;
}
