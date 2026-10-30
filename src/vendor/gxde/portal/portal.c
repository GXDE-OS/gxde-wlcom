// SPDX-FileCopyrightText: 2026 GXDE OS Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GXDE Portal: turn a drag into a native drop into any window.
//
// The overview (present_windows) is opened in "portal" mode: pure visuals, no
// grabs.  This module owns a wlcom-level pointer grab instead and forwards the
// drag to the window under the cursor with window-local coordinates, so the
// target application receives a real wl_data_device drop at the cursor.

#define _POSIX_C_SOURCE 200809L

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>

#include <kywc/log.h>

#include "input/input.h"
#include "../../../input/input_p.h"
#include "input/seat.h"
#include "server.h"
#include "util/dbus.h"

#include "../../../view/view_p.h"
#include "portal.h"

struct portal {
    struct server *server;
    struct view_manager *view_manager;
    struct seat *seat;

    struct wlr_drag *drag;
    struct view *target;
    char target_uuid[128];
    bool active;
    bool dropped;
    struct wl_event_source *finish_idle;
    char pending_uuid[128];
    bool pending_dropped;

    struct wl_listener start_drag;
    struct wl_listener seat_destroy;
    struct wl_listener drag_destroy;
    struct wl_listener drag_drop;
    struct wl_listener server_destroy;

    struct seat_pointer_grab grab;
};

static struct portal *portal = NULL;

struct view_manager *portal_view_manager(void)
{
    return portal ? portal->view_manager : NULL;
}

static void portal_end_overview(void)
{
    present_windows_portal_end();
}

static bool portal_grab_motion(struct seat_pointer_grab *grab, uint32_t time, double lx, double ly)
{
    struct portal *p = grab->data;
    if (!p->active) {
        return false;
    }

    /* cursor_feed_motion() returns early for grabbed motion, keep the drag
     * icon following the cursor ourselves. */
    selection_handle_cursor_move(p->seat, (int)lx, (int)ly);

    double sx = 0.0;
    double sy = 0.0;
    struct view *view = present_windows_portal_view_at(lx, ly, &sx, &sy);

    if (view != p->target) {
        p->target = view;
        if (view && view->surface) {
            snprintf(p->target_uuid, sizeof(p->target_uuid), "%s",
                     view->base.uuid ? view->base.uuid : "");
            /* drives wlr_drag_set_focus(): the target client receives
             * wl_data_device.enter with window-local coordinates. */
            wlr_seat_pointer_notify_enter(p->seat->wlr_seat, view->surface, sx, sy);
        } else {
            p->target_uuid[0] = '\0';
            wlr_seat_pointer_notify_clear_focus(p->seat->wlr_seat);
        }
        return true;
    }

    if (view) {
        wlr_seat_pointer_notify_motion(p->seat->wlr_seat, time, sx, sy);
    }
    return true;
}

static void portal_grab_cancel(struct seat_pointer_grab *grab)
{
    struct portal *p = grab->data;
    p->active = false;
    p->target = NULL;
    p->target_uuid[0] = '\0';
}

static bool portal_grab_button(struct seat_pointer_grab *grab, uint32_t time, uint32_t button,
                               bool pressed)
{
    struct portal *p = grab->data;
    if (!p->active) {
        return false;
    }
    if (button != BTN_LEFT || pressed) {
        return true;
    }

    /* Release the grab before forwarding so the release reaches the drag and
     * not this handler again.  drag_destroy() finishes the portal. */
    seat_end_pointer_grab(p->seat, &p->grab);
    wlr_seat_pointer_notify_button(p->seat->wlr_seat, time, button,
                                   WL_POINTER_BUTTON_STATE_RELEASED);
    return true;
}

static const struct seat_pointer_grab_interface portal_grab_impl = {
    .motion = portal_grab_motion,
    .button = portal_grab_button,
    .cancel = portal_grab_cancel,
};

bool portal_begin(void)
{
    if (!portal || !portal->drag || portal->active) {
        return portal && portal->active;
    }

    if (!present_windows_portal_begin()) {
        kywc_log(KYWC_DEBUG, "(Portal) no window to present");
        return false;
    }

    portal->active = true;
    portal->target = NULL;
    portal->target_uuid[0] = '\0';
    portal->grab = (struct seat_pointer_grab){
        .interface = &portal_grab_impl,
        .seat = portal->seat,
        .data = portal,
    };
    seat_start_pointer_grab(portal->seat, &portal->grab);
    kywc_log(KYWC_DEBUG, "(Portal) begin");
    return true;
}

void portal_cancel(void)
{
    if (!portal || !portal->active) {
        return;
    }

    portal->active = false;
    portal->target = NULL;
    portal->target_uuid[0] = '\0';
    seat_end_pointer_grab(portal->seat, &portal->grab);
    wlr_seat_pointer_notify_clear_focus(portal->seat->wlr_seat);
    portal_end_overview();
    portal_emit_dropped("", false);
    kywc_log(KYWC_DEBUG, "(Portal) cancelled");
}

static void finish_portal_drop(void *data)
{
    struct portal *p = data;
    p->finish_idle = NULL;
    if (portal != p) {
        return;
    }

    char uuid[sizeof(p->pending_uuid)];
    snprintf(uuid, sizeof(uuid), "%s", p->pending_uuid);
    const bool dropped = p->pending_dropped;
    p->pending_uuid[0] = '\0';
    p->pending_dropped = false;

    /* Closing the overview and emitting D-Bus signals from inside the drag
     * destroy signal re-enters wlroots while it is tearing the drag down.
     * Do it from an idle callback instead. */
    portal_end_overview();
    portal_emit_dropped(uuid, dropped);
    portal_emit_drag_ended();
    kywc_log(KYWC_DEBUG, "(Portal) drop finished uuid=%s accepted=%d", uuid, dropped);
}

static void handle_drag_destroy(struct wl_listener *listener, void *data)
{
    struct portal *p = wl_container_of(listener, p, drag_destroy);
    wl_list_remove(&p->drag_destroy.link);
    wl_list_remove(&p->drag_drop.link);
    p->drag = NULL;

    const bool was_active = p->active;
    const bool dropped = p->dropped;
    char uuid[sizeof(p->target_uuid)];
    snprintf(uuid, sizeof(uuid), "%s", p->target_uuid);

    p->active = false;
    p->dropped = false;
    p->target = NULL;
    p->target_uuid[0] = '\0';

    if (was_active) {
        seat_end_pointer_grab(p->seat, &p->grab);
        snprintf(p->pending_uuid, sizeof(p->pending_uuid), "%s", uuid);
        p->pending_dropped = dropped;
        if (p->finish_idle) {
            wl_event_source_remove(p->finish_idle);
            p->finish_idle = NULL;
        }
        p->finish_idle = wl_event_loop_add_idle(
            wl_display_get_event_loop(p->server->display), finish_portal_drop, p);
    } else {
        portal_emit_drag_ended();
    }
}

static void handle_drag_drop(struct wl_listener *listener, void *data)
{
    struct portal *p = wl_container_of(listener, p, drag_drop);
    p->dropped = true;
}

static char *join_mime_types(struct wlr_data_source *source)
{
    if (!source) {
        return strdup("");
    }

    size_t size = 0;
    char **mime;
    wl_array_for_each(mime, &source->mime_types) {
        size += strlen(*mime) + 1;
    }

    char *result = calloc(1, size + 1);
    if (!result) {
        return NULL;
    }
    wl_array_for_each(mime, &source->mime_types) {
        if (result[0]) {
            strncat(result, ";", size - strlen(result));
        }
        strncat(result, *mime, size - strlen(result));
    }
    return result;
}

static void handle_start_drag(struct wl_listener *listener, void *data)
{
    struct portal *p = wl_container_of(listener, p, start_drag);
    struct wlr_drag *drag = data;
    if (p->drag) {
        return;
    }

    p->drag = drag;
    p->dropped = false;
    p->drag_destroy.notify = handle_drag_destroy;
    wl_signal_add(&drag->events.destroy, &p->drag_destroy);
    p->drag_drop.notify = handle_drag_drop;
    wl_signal_add(&drag->events.drop, &p->drag_drop);

    char *mimes = join_mime_types(drag->source);
    portal_emit_drag_started(mimes ? mimes : "");
    free(mimes);
}

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct portal *p = wl_container_of(listener, p, seat_destroy);

    if (p->drag) {
        wl_list_remove(&p->drag_destroy.link);
        wl_list_remove(&p->drag_drop.link);
        p->drag = NULL;
    }
    wl_list_remove(&p->start_drag.link);
    wl_list_remove(&p->seat_destroy.link);
    p->seat = NULL;
    p->active = false;
    p->target = NULL;
    p->target_uuid[0] = '\0';
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct portal *p = wl_container_of(listener, p, server_destroy);

    portal_dbus_destroy();

    if (p->drag) {
        wl_list_remove(&p->drag_destroy.link);
        wl_list_remove(&p->drag_drop.link);
        p->drag = NULL;
    }
    if (p->finish_idle) {
        wl_event_source_remove(p->finish_idle);
        p->finish_idle = NULL;
    }
    if (p->seat) {
        wl_list_remove(&p->start_drag.link);
        wl_list_remove(&p->seat_destroy.link);
        p->seat = NULL;
    }
    wl_list_remove(&p->server_destroy.link);
    free(p);
    portal = NULL;
}

void portal_create(struct view_manager *view_manager)
{
    if (portal || !view_manager || !view_manager->server) {
        return;
    }

    struct seat *seat = input_manager_get_default_seat();
    if (!seat || !seat->wlr_seat) {
        kywc_log(KYWC_ERROR, "(Portal) no default seat, portal disabled");
        return;
    }

    struct portal *p = calloc(1, sizeof(*p));
    if (!p) {
        return;
    }
    p->server = view_manager->server;
    p->view_manager = view_manager;
    p->seat = seat;

    p->start_drag.notify = handle_start_drag;
    wl_signal_add(&seat->wlr_seat->events.start_drag, &p->start_drag);
    p->seat_destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &p->seat_destroy);

    p->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(p->server, &p->server_destroy);

    portal = p;

    if (!portal_dbus_create()) {
        kywc_log(KYWC_ERROR, "(Portal) failed to register D-Bus interface");
    }

    kywc_log(KYWC_INFO, "(Portal) drag-and-drop portal ready");
}
