// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pango/pangocairo.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-login.h>

#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/util/log.h>

#include <kywc/log.h>

#include "backend/backend.h"
#include "config.h"
#include "effect/effect.h"
#include "app_id_resolver.h"
#include "gxde_identifier.h"
#include "input/input.h"
#include "output.h"
#include "plugin.h"
#include "render/renderer.h"
#include "security.h"
#include "server.h"
#include "theme.h"
#include "util/dbus.h"
#include "util/sysfs.h"
#include "view/view.h"
#include "xwayland.h"

static int prepare_for_sleep(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    struct server *server = userdata;

    /* "b" apparently reads into an int, not a bool */
    int going_down = 1;
    int ret = sd_bus_message_read(msg, "b", &going_down);
    if (ret < 0) {
        kywc_log(KYWC_WARN, "Failed to parse D-Bus response for Inhibit: %s", strerror(-ret));
        return 0;
    }
    if (!going_down) {
        wl_signal_emit_mutable(&server->events.resume, NULL);
    } else {
        wl_signal_emit_mutable(&server->events.suspend, NULL);
    }
    return 0;
}

void server_add_destroy_listener(struct server *server, struct wl_listener *listener)
{
    struct wl_signal *signal = &server->events.destroy;
    wl_list_insert(&signal->listener_list, &listener->link);
}

static void kywc_log_callback(enum wlr_log_importance verbosity, const char *fmt, va_list args)
{
    /* switch wlr_log_importance to kywc_log_level */
    enum kywc_log_level level = KYWC_WARN;
    switch (verbosity) {
    case WLR_SILENT:
        level = KYWC_SILENT;
        break;
    case WLR_ERROR:
        level = KYWC_ERROR;
        break;
    case WLR_INFO:
        level = KYWC_INFO;
        break;
    case WLR_DEBUG:
        level = KYWC_DEBUG;
        break;
    default:
        break;
    }
    kywc_vlog(level, fmt, args);
}

static void server_get_active_vt(struct server *server)
{
    char buf[64] = { 0 };
    size_t len = sysfs_read_data("/sys/class/tty/tty0/active", buf, 63);
    if (len > 0) {
        sscanf(buf, "tty%u", &server->vtnr);
        kywc_log(KYWC_INFO, "Current VT is %u", server->vtnr);
    } else {
        server->vtnr = 0;
    }
}

static void handle_session_active(struct wl_listener *listener, void *data)
{
    struct server *server = wl_container_of(listener, server, session_active);
    struct wlr_session *session = server->session;
    kywc_log(KYWC_INFO, "session becomes %sactive", session->active ? "" : "in");
    server->active = session->active;
    wl_signal_emit_mutable(&server->events.active, NULL);
}

static bool wlroots_server_init(struct server *server)
{
    /* verbosity is not used when we replaced log_callback */
    wlr_log_init(WLR_DEBUG, kywc_log_callback);

    server->backend = ky_backend_autocreate(server->display, &server->session);
    if (!server->backend) {
        kywc_log(KYWC_FATAL, "unable to create backend");
        return false;
    }

    if (server->session) {
        server->active = server->session->active;
        server->session_active.notify = handle_session_active;
        wl_signal_add(&server->session->events.active, &server->session_active);
        server_get_active_vt(server);
    } else {
        // mark active if in nested backend
        server->active = true;
    }

    server->headless_backend = wlr_headless_backend_create(server->display);
    if (!server->headless_backend) {
        kywc_log(KYWC_FATAL, "unable to create headless backend");
        return false;
    }
    wlr_multi_backend_add(server->backend, server->headless_backend);

    server->renderer = ky_renderer_autocreate(server->backend);
    if (!server->renderer) {
        kywc_log(KYWC_FATAL, "unable to create renderer");
        return false;
    }

    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (!server->allocator) {
        kywc_log(KYWC_FATAL, "unable to create allocator");
        return false;
    }

    // TODO: set renderer to NULL, drop wlr_client_buffer
    server->compositor = wlr_compositor_create(server->display, 6, server->renderer);
    wlr_subcompositor_create(server->display);

    ky_renderer_init_wl_display(server->renderer, server->backend, server->display,
                                &server->linux_dmabuf_v1);

    /**
     * Explicit sync (backported from wp_linux_drm_syncobj_v1) ->
     * GPU clients like Chromium/Firefox provide acquire/release fences so
     * that the compositor waits for their render to finish before sampling.
     *
     * Without it those clients fall back to implicit sync. That is unreliable
     * on NVIDIA and across a hybrid GPU split, so we sample buffers the client
     * has not finished rendering into and they show up transparent or fully
     * black -- the flicker seen while scrolling in Chromium or animating in
     * Flutter apps.
     *
     * Both halves of the fence handshake are in place now:
     *   - acquire: waited GPU-side via eglWaitSync in the render pass, see
     *     scene_buffer_wait_acquire() in scene/buffer.c.
     *   - release: deferred to wlr_buffer.events.release, see
     *     scene_surface_signal_release() in scene/surface.c.
     *
     * So this is ENABLED BY DEFAULT. Set KYWC_SYNCOBJ=0 to fall back to
     * implicit sync when debugging.
     * ------------------------------------------------------------------------
     * 显式同步 (从wp_linux_drm_syncobj_v1 backport) ->
     * 像是Chromium/Firefox这样的GPU客户端会提供acquire/release fences，
     * 以便合成器在采样前等待其渲染完成。
     *
     * 如果没有它，这些客户端会退回到隐式同步。而隐式同步在NVIDIA上以及跨双显卡
     * 场景下并不可靠，于是我们会采样到客户端尚未渲染完成的buffer，它们看起来就是
     * 透明或全黑 —— 也就是在Chromium里滚动、或Flutter程序播放动画时看到的闪烁。
     *
     * 目前fence握手的两侧都已就位：
     *   - acquire：在渲染通道中通过eglWaitSync于GPU端等待，参见
     *     scene/buffer.c中的scene_buffer_wait_acquire()。
     *   - release：推迟至wlr_buffer.events.release，参见
     *     scene/surface.c中的scene_surface_signal_release()。
     *
     * 因此默认启用。调试时可设置KYWC_SYNCOBJ=0以退回隐式同步。
     */
    const char *syncobj_env = getenv("KYWC_SYNCOBJ");
    if (syncobj_env == NULL || strcmp(syncobj_env, "0") != 0) {
        int syncobj_drm_fd = wlr_backend_get_drm_fd(server->backend);
        if (syncobj_drm_fd >= 0) {
            if (wlr_linux_drm_syncobj_manager_v1_create(server->display, 1, syncobj_drm_fd)) {
                kywc_log(KYWC_INFO, "Enabled linux-drm-syncobj-v1 (explicit sync)");
            }
        }
    }

    server->layout = wlr_output_layout_create();
    server->scene = ky_scene_create();
    server->scene_layout = ky_scene_attach_output_layout(server->scene, server->layout);
    if (server->linux_dmabuf_v1) {
        ky_scene_set_linux_dmabuf_v1(server->scene, server->linux_dmabuf_v1);
    }

    struct wlr_tearing_control_manager_v1 *tearing_control_v1 =
        wlr_tearing_control_manager_v1_create(server->display, 1);
    if (tearing_control_v1) {
        ky_scene_set_tearing_control_v1(server->scene, tearing_control_v1);
    }

    struct wlr_presentation *presentation =
        wlr_presentation_create(server->display, server->backend);
    if (presentation) {
        ky_scene_set_presentation(server->scene, presentation);
    }

    wlr_screencopy_manager_v1_create(server->display);
    wlr_export_dmabuf_manager_v1_create(server->display);
    wlr_viewporter_create(server->display);
    wlr_fractional_scale_manager_v1_create(server->display, 1);

    return true;
}

static int handle_exit(int signal, void *data)
{
    kywc_log(KYWC_SILENT, "Received signal %s(%u), exit...", strsignal(signal), signal);
    struct server *server = data;
    wl_display_terminate(server->display);
    return 0;
}

bool server_init(struct server *server)
{
    server->display = wl_display_create();
    server->event_loop = wl_display_get_event_loop(server->display);

    wl_signal_init(&server->events.ready);
    wl_signal_init(&server->events.start);
    wl_signal_init(&server->events.terminate);
    wl_signal_init(&server->events.destroy);
    wl_signal_init(&server->events.suspend);
    wl_signal_init(&server->events.resume);
    wl_signal_init(&server->events.active);

    /* use wl event source to sync these signals in multi-thread */
    server->sources.sighup =
        wl_event_loop_add_signal(server->event_loop, SIGHUP, handle_exit, server);
    server->sources.sigterm =
        wl_event_loop_add_signal(server->event_loop, SIGTERM, handle_exit, server);

    dbus_context_create(server);
    dbus_match_system_signal("org.freedesktop.login1", "/org/freedesktop/login1",
                             "org.freedesktop.login1.Manager", "PrepareForSleep", prepare_for_sleep,
                             server);

    server->queue = queue_create(64, 4, server);

    config_manager_create(server);
    security_manager_create(server);
    theme_manager_create(server);
    gxde_identifier_create(server);
    app_id_resolver_manager_create(server);

    if (!wlroots_server_init(server)) {
        return false;
    }

    output_manager_create(server);
    input_manager_create(server);
    view_manager_create(server);
    xwayland_server_create(server);

    effect_manager_create(server);
    plugin_manager_create(server);

    server->ready = true;
    wl_signal_emit_mutable(&server->events.ready, NULL);

    return true;
}

bool server_start(struct server *server)
{
    /* Add a Unix socket to the Wayland display. */
    const char *socket = wl_display_add_socket_auto(server->display);
    if (!socket) {
        kywc_log_errno(KYWC_FATAL, "unable to open wayland socket");
        return false;
    }

    if (setenv("WAYLAND_DISPLAY", socket, true) < 0) {
        kywc_log_errno(KYWC_ERROR, "unable to set WAYLAND_DISPLAY");
    } else {
        kywc_log(KYWC_DEBUG, "WAYLAND_DISPLAY=%s", socket);
    }

    if (setenv("DDE_CURRENT_COMPOSITOR", "GXWM", true) < 0) {
        kywc_log_errno(KYWC_ERROR, "unable to set DDE_CURRENT_COMPOSITOR");
    } else {
        kywc_log(KYWC_DEBUG, "DDE_CURRENT_COMPOSITOR=GXWM");
    }

    if (!wlr_backend_start(server->backend)) {
        kywc_log(KYWC_FATAL, "unable to start the wlroots backend");
        return false;
    }

    server->start = true;
    wl_signal_emit_mutable(&server->events.start, NULL);

    return true;
}

void server_run(struct server *server)
{
    kywc_log(KYWC_INFO, "Running wayland compositor on wayland display '%s'",
             getenv("WAYLAND_DISPLAY"));
    wl_display_run(server->display);
}

void server_finish(struct server *server)
{
    server->terminate = true;

    wl_event_source_remove(server->sources.sighup);
    wl_event_source_remove(server->sources.sigterm);

    wl_signal_emit_mutable(&server->events.terminate, NULL);

    queue_destroy(server->queue);

    wl_display_destroy_clients(server->display);
    /* make sure all xwayland-shells are destroyed */
    xwayland_server_destroy();

    wl_display_destroy(server->display);

    /* call all server_destroy listeners */
    wl_signal_emit_mutable(&server->events.destroy, NULL);

    /* scene may be NULL when server_init failed */
    if (server->scene) {
        ky_scene_node_destroy(&server->scene->tree.node);
    }
    wlr_output_layout_destroy(server->layout);
    wlr_allocator_destroy(server->allocator);
    wlr_renderer_destroy(server->renderer);
    /* free memory in fontconfig */
    pango_cairo_font_map_set_default(NULL);

    kywc_log(KYWC_SILENT, "gxde-wlcom finished...\n");
}
