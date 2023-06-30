#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>

#include <pango/pangocairo.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-login.h>

#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
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

#include "config.h"
#include "input/input.h"
#include "output.h"
#include "plugin.h"
#include "server.h"
#include "theme.h"
#include "view/view.h"
#include "view/xwayland.h"

static const char *dbus_logind_service = "org.freedesktop.login1";
static const char *dbus_logind_path = "/org/freedesktop/login1";
static const char *dbus_logind_manager_interface = "org.freedesktop.login1.Manager";

static int dbus_event(int fd, uint32_t mask, void *data)
{
    struct server *server = data;
    if (mask & WL_EVENT_ERROR) {
        kywc_log(KYWC_ERROR, "IPC system dbus error");
        return 0;
    }
    if (mask & WL_EVENT_HANGUP) {
        kywc_log(KYWC_DEBUG, "System dbus hung up");
        return 0;
    }

    while (sd_bus_process(server->sys_bus, NULL) > 0) {
        ;
    }
    return 0;
}

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

static void listen_logind_manager_signal(struct server *server)
{
    int ret = sd_bus_default_system(&server->sys_bus);
    if (ret < 0) {
        kywc_log(KYWC_ERROR, "Failed to connect to system bus: %s", strerror(-ret));
        return;
    }

    int fd = sd_bus_get_fd(server->sys_bus);
    server->dbus =
        wl_event_loop_add_fd(server->event_loop, fd, WL_EVENT_READABLE, dbus_event, server);
    wl_event_source_check(server->dbus);

    ret = sd_bus_match_signal(server->sys_bus, NULL, dbus_logind_service, dbus_logind_path,
                              dbus_logind_manager_interface, "PrepareForSleep", prepare_for_sleep,
                              server);
    if (ret < 0) {
        kywc_log(KYWC_ERROR, "Failed to add D-Bus signal match : sleep");
    }
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

static bool wlroots_server_init(struct server *server)
{
    /* verbosity is not used when we replaced log_callback */
    wlr_log_init(WLR_DEBUG, kywc_log_callback);

    server->backend = wlr_backend_autocreate(server->display, &server->session);
    if (!server->backend) {
        kywc_log(KYWC_ERROR, "unable to create backend");
        return false;
    }

    server->headless_backend = wlr_headless_backend_create(server->display);
    if (!server->headless_backend) {
        kywc_log(KYWC_ERROR, "unable to create headless backend");
        return false;
    }
    wlr_multi_backend_add(server->backend, server->headless_backend);

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) {
        kywc_log(KYWC_ERROR, "unable to create renderer");
        return false;
    }

    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (!server->allocator) {
        kywc_log(KYWC_ERROR, "unable to create allocator");
        return false;
    }

    // TODO: set renderer to NULL, drop wlr_client_buffer
    server->compositor = wlr_compositor_create(server->display, 6, server->renderer);
    wlr_subcompositor_create(server->display);
    wlr_renderer_init_wl_display(server->renderer, server->display);

    server->layout = wlr_output_layout_create();
    server->scene = ky_scene_create(server);
    ky_scene_attach_output_layout(server->scene, server->layout);

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

bool server_init(struct server *server)
{
    server->display = wl_display_create();
    server->event_loop = wl_display_get_event_loop(server->display);

    wl_signal_init(&server->events.ready);
    wl_signal_init(&server->events.destroy);
    wl_signal_init(&server->events.suspend);
    wl_signal_init(&server->events.resume);

    listen_logind_manager_signal(server);

    config_manager_create(server);

    if (!wlroots_server_init(server)) {
        return false;
    }

    theme_manager_create(server);
    output_manager_create(server);
    input_manager_create(server);
    view_manager_create(server);
    xwayland_server_create(server);

    plugin_manager_create(server);

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

    setenv("WAYLAND_DISPLAY", socket, true);
    if (setenv("WAYLAND_DISPLAY", socket, true) < 0) {
        kywc_log_errno(KYWC_ERROR, "unable to set WAYLAND_DISPLAY");
    } else {
        kywc_log(KYWC_DEBUG, "WAYLAND_DISPLAY=%s", socket);
    }

    if (!wlr_backend_start(server->backend)) {
        kywc_log(KYWC_ERROR, "unable to start the wlroots backend");
        return false;
    }

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
    wl_event_source_remove(server->dbus);

    /* make sure all xwayland-shells are destroyed */
    xwayland_server_destroy();
    wl_display_destroy_clients(server->display);

    wl_display_destroy(server->display);

    ky_scene_destroy(server->scene);
    wlr_output_layout_destroy(server->layout);
    wlr_allocator_destroy(server->allocator);
    wlr_renderer_destroy(server->renderer);

    wl_signal_emit_mutable(&server->events.destroy, NULL);

    /* free memory in fontconfig */
    pango_cairo_font_map_set_default(NULL);
    kywc_log(KYWC_SILENT, "kylin-wlcom finished...\n");
}
