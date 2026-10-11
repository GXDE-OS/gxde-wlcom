#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/util/log.h>
#include <xcb/xfixes.h>
#include "xwayland/xwm.h"
#include "xwayland/selection.h"

static xcb_atom_t data_device_manager_dnd_action_to_atom(
		struct wlr_xwm *xwm, enum wl_data_device_manager_dnd_action action) {
	if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY) {
		return xwm->atoms[DND_ACTION_COPY];
	} else if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE) {
		return xwm->atoms[DND_ACTION_MOVE];
	} else if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK) {
		return xwm->atoms[DND_ACTION_ASK];
	}
	return XCB_ATOM_NONE;
}

static enum wl_data_device_manager_dnd_action
		data_device_manager_dnd_action_from_atom(struct wlr_xwm *xwm,
		enum atom_name atom) {
	if (atom == xwm->atoms[DND_ACTION_COPY] ||
			atom == xwm->atoms[DND_ACTION_PRIVATE]) {
		return WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
	} else if (atom == xwm->atoms[DND_ACTION_MOVE]) {
		return WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
	} else if (atom == xwm->atoms[DND_ACTION_ASK]) {
		return WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
	}
	return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
}

static void xwm_dnd_send_event(struct wlr_xwm *xwm, xcb_atom_t type,
		xcb_client_message_data_t *data) {
	struct wlr_xwayland_surface *dest = xwm->drag_focus;
	assert(dest != NULL);

	xcb_client_message_event_t event = {
		.response_type = XCB_CLIENT_MESSAGE,
		.format = 32,
		.sequence = 0,
		.window = dest->window_id,
		.type = type,
		.data = *data,
	};

	xwm_send_event_with_size(xwm->xcb_conn,
		0, // propagate
		dest->proxy_window ? dest->proxy_window : dest->window_id,
		XCB_EVENT_MASK_NO_EVENT,
		&event,
		sizeof(event));
	xcb_flush(xwm->xcb_conn);
}

static void xwm_dnd_send_enter(struct wlr_xwm *xwm) {
	struct wlr_drag *drag = xwm->drag;
	assert(drag != NULL);
	struct wl_array *mime_types = &drag->source->mime_types;

	xcb_client_message_data_t data = { 0 };
	data.data32[0] = xwm->dnd_selection.window;
	data.data32[1] = XDND_VERSION << 24;

	// If we have 3 MIME types or less, we can send them directly in the
	// DND_ENTER message
	size_t n = mime_types->size / sizeof(char *);
	if (n <= 3) {
		size_t i = 0;
		char **mime_type_ptr;
		wl_array_for_each(mime_type_ptr, mime_types) {
			char *mime_type = *mime_type_ptr;
			data.data32[2+i] = xwm_mime_type_to_atom(xwm, mime_type);
			++i;
		}
	} else {
		// Let the client know that targets are not contained in the message
		// data and must be retrieved with the DND_TYPE_LIST property
		data.data32[1] |= 1;

		xcb_atom_t targets[n];
		size_t i = 0;
		char **mime_type_ptr;
		wl_array_for_each(mime_type_ptr, mime_types) {
			char *mime_type = *mime_type_ptr;
			targets[i] = xwm_mime_type_to_atom(xwm, mime_type);
			++i;
		}

		xcb_change_property(xwm->xcb_conn,
			XCB_PROP_MODE_REPLACE,
			xwm->dnd_selection.window,
			xwm->atoms[DND_TYPE_LIST],
			XCB_ATOM_ATOM,
			32, // format
			n, targets);
	}

	xwm_dnd_send_event(xwm, xwm->atoms[DND_ENTER], &data);
}

static void xwm_dnd_send_position(struct wlr_xwm *xwm, uint32_t time, int16_t x,
		int16_t y) {
	struct wlr_drag *drag = xwm->drag;
	assert(drag != NULL);

	xcb_client_message_data_t data = { 0 };
	data.data32[0] = xwm->dnd_selection.window;
	data.data32[2] = (x << 16) | y;
	data.data32[3] = time;
	data.data32[4] =
		data_device_manager_dnd_action_to_atom(xwm, drag->source->actions);

	xwm_dnd_send_event(xwm, xwm->atoms[DND_POSITION], &data);
}

static void xwm_dnd_send_drop(struct wlr_xwm *xwm, uint32_t time) {
	struct wlr_drag *drag = xwm->drag;
	assert(drag != NULL);
	struct wlr_xwayland_surface *dest = xwm->drag_focus;
	assert(dest != NULL);

	xcb_client_message_data_t data = { 0 };
	data.data32[0] = xwm->dnd_selection.window;
	data.data32[2] = time;

	xwm_dnd_send_event(xwm, xwm->atoms[DND_DROP], &data);
}

static void xwm_dnd_send_leave(struct wlr_xwm *xwm) {
	struct wlr_drag *drag = xwm->drag;
	assert(drag != NULL);
	struct wlr_xwayland_surface *dest = xwm->drag_focus;
	assert(dest != NULL);

	xcb_client_message_data_t data = { 0 };
	data.data32[0] = xwm->dnd_selection.window;

	xwm_dnd_send_event(xwm, xwm->atoms[DND_LEAVE], &data);
}

/*static void xwm_dnd_send_finished(struct wlr_xwm *xwm) {
	struct wlr_drag *drag = xwm->drag;
	assert(drag != NULL);
	struct wlr_xwayland_surface *dest = xwm->drag_focus;
	assert(dest != NULL);

	xcb_client_message_data_t data = { 0 };
	data.data32[0] = xwm->dnd_selection.window;
	data.data32[1] = drag->source->accepted;

	if (drag->source->accepted) {
		data.data32[2] = data_device_manager_dnd_action_to_atom(xwm,
			drag->source->current_dnd_action);
	}

	xwm_dnd_send_event(xwm, xwm->atoms[DND_FINISHED], &data);
}*/

int xwm_handle_selection_client_message(struct wlr_xwm *xwm,
		xcb_client_message_event_t *ev) {
	if (ev->type == xwm->atoms[DND_STATUS]) {
		if (xwm->drag == NULL) {
			wlr_log(WLR_DEBUG, "ignoring XdndStatus client message because "
				"there's no drag");
			return 1;
		}

		xcb_client_message_data_t *data = &ev->data;
		xcb_window_t target_window = data->data32[0];
		bool accepted = data->data32[1] & 1;
		xcb_atom_t action_atom = data->data32[4];

		if (xwm->drag_focus == NULL ||
				target_window != xwm->drag_focus->window_id) {
			wlr_log(WLR_DEBUG, "ignoring XdndStatus client message because "
				"it doesn't match the current drag focus window ID");
			return 1;
		}

		enum wl_data_device_manager_dnd_action action =
			data_device_manager_dnd_action_from_atom(xwm, action_atom);

		struct wlr_drag *drag = xwm->drag;
		assert(drag != NULL);

		drag->source->accepted = accepted;
		wl_signal_emit_mutable(&drag->source->events.accepted, NULL);

		wlr_data_source_dnd_action(drag->source, action);

		drag->pos.waiting = false;
		if (drag->pos.cached) {
			drag->pos.cached = false;
			xwm_dnd_send_position(xwm, XCB_CURRENT_TIME, drag->pos.cache_x, drag->pos.cache_y);
		}

		wlr_log(WLR_DEBUG, "DND_STATUS window=%" PRIu32 " accepted=%d action=%d",
			target_window, accepted, action);
		return 1;
	} else if (ev->type == xwm->atoms[DND_FINISHED]) {
		// This should only happen after the drag has ended, but before the drag
		// source is destroyed
		if (xwm->seat == NULL || xwm->seat->drag_source == NULL ||
				xwm->drag != NULL) {
			wlr_log(WLR_DEBUG, "ignoring XdndFinished client message because "
				"there's no finished drag");
			return 1;
		}

		struct wlr_data_source *source = xwm->seat->drag_source;

		xcb_client_message_data_t *data = &ev->data;
		xcb_window_t target_window = data->data32[0];
		bool performed = data->data32[1] & 1;
		xcb_atom_t action_atom = data->data32[2];

		if (xwm->drop_focus == NULL ||
				target_window != xwm->drop_focus->window_id) {
			wlr_log(WLR_DEBUG, "ignoring XdndFinished client message because "
				"it doesn't match the finished drop focus window ID");
			return 1;
		}

		enum wl_data_device_manager_dnd_action action =
			data_device_manager_dnd_action_from_atom(xwm, action_atom);

		wlr_data_source_dnd_finish(source);
		wlr_log(WLR_DEBUG, "DND_FINISH window=%" PRIu32 " performed=%d action=%d",
			target_window, performed, action);
		return 1;
	} else {
		return 0;
	}
}

static void xwm_set_drag_focus(struct wlr_xwm *xwm, struct wlr_xwayland_surface *focus);

static void drag_focus_handle_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, drag_focus_destroy);
	xwm_set_drag_focus(xwm, NULL);
}

static void drop_focus_handle_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, drop_focus_destroy);
	wl_list_remove(&xwm->drop_focus_destroy.link);
	wl_list_init(&xwm->drop_focus_destroy.link);
	xwm->drop_focus = NULL;
}

static bool property_reply_is_valid(xcb_get_property_reply_t *reply) {
    return reply && reply->type == XCB_ATOM_WINDOW && reply->format == 32 &&
           xcb_get_property_value_length(reply) == sizeof(xcb_window_t);
}

static xcb_window_t get_proxy_window(struct wlr_xwm *xwm, xcb_window_t window) {
    xcb_window_t target_window = window;
    xcb_get_property_cookie_t proxy_cookie =
        xcb_get_property(xwm->xcb_conn, 0, window, xwm->atoms[DND_PROXY], XCB_ATOM_WINDOW, 0, 1);
    xcb_get_property_reply_t *proxy_reply =
        xcb_get_property_reply(xwm->xcb_conn, proxy_cookie, NULL);

    if (property_reply_is_valid(proxy_reply)) {
        xcb_window_t proxy_window = *(xcb_window_t *)xcb_get_property_value(proxy_reply);

        xcb_get_property_cookie_t proxy_verify_cookie = xcb_get_property(
            xwm->xcb_conn, 0, proxy_window, xwm->atoms[DND_PROXY], XCB_ATOM_WINDOW, 0, 1);
        xcb_get_property_reply_t *proxy_verify_reply =
            xcb_get_property_reply(xwm->xcb_conn, proxy_verify_cookie, NULL);

        if (property_reply_is_valid(proxy_verify_reply)) {
            xcb_window_t verifyWindow = *(xcb_window_t *)xcb_get_property_value(proxy_verify_reply);
            if (verifyWindow == proxy_window) {
                target_window = proxy_window;
            }
        }
        free(proxy_verify_reply);
    }

    free(proxy_reply);
    return target_window;
}

static void xwm_set_drag_focus(struct wlr_xwm *xwm, struct wlr_xwayland_surface *focus) {
	if (focus == xwm->drag_focus) {
		return;
	}

	// clear pos flags
	xwm->drag->pos.waiting = false;
	xwm->drag->pos.cached = false;

	if (xwm->drag_focus != NULL) {
		wlr_data_source_dnd_action(xwm->drag->source,
			WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE);
		xwm_dnd_send_leave(xwm);
	}

	wl_list_remove(&xwm->drag_focus_destroy.link);
	wl_list_init(&xwm->drag_focus_destroy.link);

	xwm->drag_focus = focus;

	if (xwm->drag_focus != NULL) {
		xwm->drag_focus_destroy.notify = drag_focus_handle_destroy;
		wl_signal_add(&xwm->drag_focus->events.destroy, &xwm->drag_focus_destroy);

		if (!xwm->drag_focus->proxy_window) {
			xwm->drag_focus->proxy_window = get_proxy_window(xwm, focus->window_id);
		}

		xwm_dnd_send_enter(xwm);
	}
}

static void seat_handle_drag_focus(struct wl_listener *listener, void *data) {
	struct wlr_drag *drag = data;
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, seat_drag_focus);

	// dnd_leave will be sent when drag destroy if necessary
	if (drag->cancelling) {
		return;
	}

	struct wlr_xwayland_surface *focus = NULL;
	if (drag->focus != NULL) {
		focus = wlr_xwayland_surface_try_from_wlr_surface(drag->focus);
	}

	xwm_set_drag_focus(xwm, focus);
}

static void seat_handle_drag_motion(struct wl_listener *listener, void *data) {
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, seat_drag_motion);
	struct wlr_drag_motion_event *event = data;
	struct wlr_xwayland_surface *surface = xwm->drag_focus;
	struct wlr_drag *drag = xwm->drag;

	if (surface == NULL) {
		return; // No xwayland surface focused
	}

	if (drag->pos.waiting) {
		drag->pos.cache_x = (int16_t)event->sx;
		drag->pos.cache_y = (int16_t)event->sy;
		drag->pos.cached = true;
		return;
	}

	drag->pos.waiting = true;

	xwm_dnd_send_position(xwm, event->time, surface->x + (int16_t)event->sx,
		surface->y + (int16_t)event->sy);
}

static void seat_handle_drag_drop(struct wl_listener *listener, void *data) {
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, seat_drag_drop);
	struct wlr_drag_drop_event *event = data;

	if (xwm->drag_focus == NULL) {
		return; // No xwayland surface focused
	}

	wlr_log(WLR_DEBUG, "Wayland drag dropped over an Xwayland window");

	xwm->drop_focus = xwm->drag_focus;
	xwm->drop_focus_destroy.notify = drop_focus_handle_destroy;
	wl_list_remove(&xwm->drop_focus_destroy.link);
	wl_signal_add(&xwm->drop_focus->events.destroy, &xwm->drop_focus_destroy);

	xwm_dnd_send_drop(xwm, event->time);
}

static void seat_handle_drag_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, seat_drag_destroy);

	// Don't reset drag focus yet because the target will read the drag source
	// right after
	if (xwm->drag_focus != NULL && !xwm->drag->source->accepted) {
		wlr_log(WLR_DEBUG, "Wayland drag cancelled over an Xwayland window");
		xwm_dnd_send_leave(xwm);
	}

	wl_list_remove(&xwm->seat_drag_focus.link);
	wl_list_remove(&xwm->seat_drag_motion.link);
	wl_list_remove(&xwm->seat_drag_drop.link);
	wl_list_remove(&xwm->seat_drag_destroy.link);
	xwm->drag = NULL;
}

static void seat_handle_drag_source_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_xwm *xwm =
		wl_container_of(listener, xwm, seat_drag_source_destroy);

	wl_list_remove(&xwm->seat_drag_source_destroy.link);
	wl_list_init(&xwm->seat_drag_source_destroy.link);
	wl_list_remove(&xwm->drag_focus_destroy.link);
	wl_list_init(&xwm->drag_focus_destroy.link);
	xwm->drag_focus = NULL;

	wl_list_remove(&xwm->drop_focus_destroy.link);
	wl_list_init(&xwm->drop_focus_destroy.link);
	xwm->drop_focus = NULL;
}

void xwm_seat_handle_start_drag(struct wlr_xwm *xwm, struct wlr_drag *drag) {
	wl_list_remove(&xwm->drag_focus_destroy.link);
	wl_list_init(&xwm->drag_focus_destroy.link);
	wl_list_remove(&xwm->drop_focus_destroy.link);
	wl_list_init(&xwm->drop_focus_destroy.link);

	xwm->drag = drag;
	xwm->drag_focus = NULL;
	xwm->drop_focus = NULL;

	if (drag != NULL) {
		wl_signal_add(&drag->events.focus, &xwm->seat_drag_focus);
		xwm->seat_drag_focus.notify = seat_handle_drag_focus;
		wl_signal_add(&drag->events.motion, &xwm->seat_drag_motion);
		xwm->seat_drag_motion.notify = seat_handle_drag_motion;
		wl_signal_add(&drag->events.drop, &xwm->seat_drag_drop);
		xwm->seat_drag_drop.notify = seat_handle_drag_drop;
		wl_signal_add(&drag->events.destroy, &xwm->seat_drag_destroy);
		xwm->seat_drag_destroy.notify = seat_handle_drag_destroy;

		wl_signal_add(&drag->source->events.destroy,
			&xwm->seat_drag_source_destroy);
		xwm->seat_drag_source_destroy.notify = seat_handle_drag_source_destroy;
	}
}

void xwm_seat_unlink_drag_handlers(struct wlr_xwm *xwm) {
	wl_list_remove(&xwm->seat_drag_source_destroy.link);
	wl_list_remove(&xwm->drag_focus_destroy.link);
	wl_list_remove(&xwm->drop_focus_destroy.link);

	if (!xwm->drag) {
		return;
	}
	wl_list_remove(&xwm->seat_drag_focus.link);
	wl_list_remove(&xwm->seat_drag_motion.link);
	wl_list_remove(&xwm->seat_drag_drop.link);
	wl_list_remove(&xwm->seat_drag_destroy.link);
}
