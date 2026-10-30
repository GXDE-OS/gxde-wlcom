// SPDX-FileCopyrightText: 2026 GXDE OS Team
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <systemd/sd-bus.h>

#include <kywc/log.h>

#include "util/dbus.h"

#include "../../../view/view_p.h"
#include "portal.h"

#define PORTAL_SERVICE "org.gxde.Portal"
#define PORTAL_PATH "/org/gxde/Portal"
#define PORTAL_INTERFACE "org.gxde.Portal"

static struct dbus_object *portal_object = NULL;

static int method_begin(sd_bus_message *message, void *data, sd_bus_error *error)
{
    int x = 0, y = 0, w = 0, h = 0;
    int r = sd_bus_message_read(message, "iiii", &x, &y, &w, &h);
    if (r < 0) {
        return r;
    }
    return sd_bus_reply_method_return(message, "b", portal_begin(x, y, w, h));
}

static int method_cancel(sd_bus_message *message, void *data, sd_bus_error *error)
{
    portal_cancel();
    return sd_bus_reply_method_return(message, NULL);
}

static int method_get_layout(sd_bus_message *message, void *data, sd_bus_error *error)
{
    sd_bus_message *reply = NULL;
    int r = sd_bus_message_new_method_return(message, &reply);
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_open_container(reply, 'a', "(sssiiii)");
    if (r < 0) {
        sd_bus_message_unref(reply);
        return r;
    }

    const size_t count = present_windows_portal_item_count();
    for (size_t i = 0; i < count; i++) {
        struct kywc_box box = { 0 };
        struct view *view = present_windows_portal_item(i, &box);
        if (!view) {
            continue;
        }
        r = sd_bus_message_append(reply, "(sssiiii)",
                                  view->base.uuid ? view->base.uuid : "",
                                  view->base.title ? view->base.title : "", "", box.x, box.y,
                                  box.width, box.height);
        if (r < 0) {
            sd_bus_message_unref(reply);
            return r;
        }
    }

    r = sd_bus_message_close_container(reply);
    if (r < 0) {
        sd_bus_message_unref(reply);
        return r;
    }

    return sd_bus_send(sd_bus_message_get_bus(reply), reply, NULL);
}

static int method_list_windows(sd_bus_message *message, void *data, sd_bus_error *error)
{
    struct view_manager *view_manager = portal_view_manager();
    sd_bus_message *reply = NULL;
    int r = sd_bus_message_new_method_return(message, &reply);
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_open_container(reply, 'a', "(sssiiii)");
    if (r < 0) {
        sd_bus_message_unref(reply);
        return r;
    }

    if (view_manager) {
        struct view *view;
        wl_list_for_each(view, &view_manager->views, link) {
            if (!view->base.mapped || !view->base.uuid) {
                continue;
            }
            r = sd_bus_message_append(reply, "(sssiiii)", view->base.uuid,
                                      view->base.title ? view->base.title : "",
                                      view->base.app_id ? view->base.app_id : "",
                                      view->base.geometry.x, view->base.geometry.y,
                                      view->base.geometry.width, view->base.geometry.height);
            if (r < 0) {
                sd_bus_message_unref(reply);
                return r;
            }
        }
    }

    r = sd_bus_message_close_container(reply);
    if (r < 0) {
        sd_bus_message_unref(reply);
        return r;
    }

    return sd_bus_send(sd_bus_message_get_bus(reply), reply, NULL);
}

static const sd_bus_vtable portal_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("BeginPortal", "iiii", "b", method_begin, 0),
    SD_BUS_METHOD("CancelPortal", "", "", method_cancel, 0),
    SD_BUS_METHOD("ListWindows", "", "a(sssiiii)", method_list_windows, 0),
    SD_BUS_METHOD("GetLayout", "", "a(sssiiii)", method_get_layout, 0),
    SD_BUS_VTABLE_END,
};

bool portal_dbus_create(void)
{
    portal_object = dbus_register_object(PORTAL_SERVICE, PORTAL_PATH, PORTAL_INTERFACE,
                                         portal_vtable, NULL);
    return portal_object != NULL;
}

void portal_dbus_destroy(void)
{
    if (portal_object) {
        dbus_unregister_object(portal_object);
        portal_object = NULL;
    }
}

void portal_emit_drag_started(const char *mime_types)
{
    dbus_emit_signal(PORTAL_PATH, PORTAL_INTERFACE, "DragStarted", "s",
                     mime_types ? mime_types : "");
}

void portal_emit_drag_ended(void)
{
    dbus_emit_signal(PORTAL_PATH, PORTAL_INTERFACE, "DragEnded", "");
}

void portal_emit_dropped(const char *uuid, bool accepted)
{
    dbus_emit_signal(PORTAL_PATH, PORTAL_INTERFACE, "PortalDropped", "sb",
                     uuid ? uuid : "", (int)accepted);
}
