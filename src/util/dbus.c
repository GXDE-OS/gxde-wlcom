// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include <kywc/log.h>

#include "server.h"
#include "util/dbus.h"
#include "util/string.h"

struct dbus_object {
    struct wl_list link;
    sd_bus_slot *slot;
};

struct dbus_context {
    /* system bus */
    struct sd_bus *system;
    struct wl_event_source *system_event;
    /* user bus */
    struct sd_bus *user;
    struct wl_event_source *user_event;

    struct wl_list objects;
    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

static struct dbus_context *context = NULL;

static int dbus_process(int fd, uint32_t mask, void *data)
{
    if (mask & WL_EVENT_ERROR) {
        kywc_log(KYWC_ERROR, "IPC Client error");
        return 0;
    }
    if (mask & WL_EVENT_HANGUP) {
        kywc_log(KYWC_DEBUG, "Client hung up");
        return 0;
    }

    struct sd_bus *bus = data;
    while (sd_bus_process(bus, NULL) > 0) {
        ;
    }
    return 0;
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&context->server_destroy.link);

    struct dbus_object *object, *tmp;
    wl_list_for_each_safe(object, tmp, &context->objects, link) {
        dbus_unregister_object(object);
    }

    sd_bus_flush_close_unref(context->system);
    sd_bus_flush_close_unref(context->user);

    free(context);
    context = NULL;
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&context->display_destroy.link);

    if (context->user_event) {
        wl_event_source_remove(context->user_event);
    }
    if (context->system_event) {
        wl_event_source_remove(context->system_event);
    }
}

bool dbus_context_create(struct server *server)
{
    context = calloc(1, sizeof(*context));
    if (!context) {
        return false;
    }

    int ret = sd_bus_open_system(&context->system);
    if (ret < 0) {
        kywc_log(KYWC_WARN, "Failed to connect to system bus: %s", strerror(-ret));
    }
    ret = sd_bus_open_user(&context->user);
    if (ret < 0) {
        kywc_log(KYWC_WARN, "Failed to connect to user bus: %s", strerror(-ret));
    }

    if (context->system) {
        int fd = sd_bus_get_fd(context->system);
        context->system_event = wl_event_loop_add_fd(server->event_loop, fd, WL_EVENT_READABLE,
                                                     dbus_process, context->system);
        wl_event_source_check(context->system_event);
    }

    if (context->user) {
        ret = sd_bus_request_name(context->user, "com.kylin.Wlcom", 0);
        if (ret < 0) {
            kywc_log(KYWC_ERROR, "Failed to acquire service name: %s", strerror(-ret));
            if (ret == -EEXIST) {
                kywc_log(KYWC_ERROR, "Is a GXDE-Wlcom already running?");
            }
            sd_bus_flush_close_unref(context->user);
            context->user = NULL;
        } else {
            int fd = sd_bus_get_fd(context->user);
            context->user_event = wl_event_loop_add_fd(server->event_loop, fd, WL_EVENT_READABLE,
                                                       dbus_process, context->user);
            wl_event_source_check(context->user_event);
        }
    }

    if (!context->system && !context->user) {
        kywc_log(KYWC_ERROR, "Failed to init system and user bus");
        free(context);
        return false;
    }

    wl_list_init(&context->objects);

    context->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &context->display_destroy);
    context->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &context->server_destroy);

    return true;
}

struct dbus_object *dbus_register_object(const char *name, const char *path, const char *interface,
                                         const sd_bus_vtable *vtable, void *data)
{
    if (!context || !context->user) {
        return NULL;
    }

    struct dbus_object *object = calloc(1, sizeof(*object));
    if (!object) {
        return NULL;
    }

    bool name_acquired = false;
    if (name) {
        int ret = sd_bus_request_name(context->user, name, 0);
        if (ret < 0 && ret != -EALREADY) {
            kywc_log(KYWC_ERROR,
                "(DBus) Accquire: Failed to acquire D-Bus service name %s. %s",
                name, strerror(-ret));
            free(object);
            return NULL;
        }

        name_acquired = ret > 0;
    }

    if (path && interface && vtable) {
        int ret =
            sd_bus_add_object_vtable(context->user, &object->slot, path, interface, vtable, data);
        if (ret < 0) {
            kywc_log(KYWC_ERROR,
                "(DBus) Register: Failed to register D-Bus interface %s at %s. %s",
                interface, path, strerror(-ret));
            if (name_acquired) {
                sd_bus_release_name(context->user, name);
            }
            free(object);
            return NULL;
        }
    }

    wl_list_insert(&context->objects, &object->link);
    return object;
}

void dbus_unregister_object(struct dbus_object *object)
{
    if (!object) {
        return;
    }

    sd_bus_slot_unref(object->slot);
    wl_list_remove(&object->link);
    free(object);
}

bool dbus_call_method(const char *service, const char *path, const char *interface,
                      const char *method, sd_bus_message_handler_t callback, void *data)
{
    if (!context || !context->user) {
        return false;
    }

    return sd_bus_call_method_async(context->user, NULL, service, path, interface, method, callback,
                                    data, NULL) >= 0;
}

bool dbus_call_system_method(const char *service, const char *path, const char *interface,
                             const char *method, sd_bus_message_handler_t callback, void *data)
{
    if (!context || !context->system) {
        return false;
    }

    return sd_bus_call_method_async(context->system, NULL, service, path, interface, method,
                                    callback, data, NULL) >= 0;
}

bool dbus_update_activation_environment(const char *name, const char *value)
{
    if (!context || !context->user || !name || !value) {
        return false;
    }

    int ret = sd_bus_call_method(context->user, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                 "org.freedesktop.DBus", "UpdateActivationEnvironment", NULL, NULL,
                                 "a{ss}", 1, name, value);
    if (ret < 0) {
        kywc_log(KYWC_WARN, "Failed to update D-Bus activation environment: %s",
                 strerror(-ret));
        return false;
    }

    char *assignment = string_create("%s=%s", name, value);
    if (!assignment) {
        return false;
    }

    ret = sd_bus_call_method(context->user, "org.freedesktop.systemd1",
                             "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager",
                             "SetEnvironment", NULL, NULL, "as", 1, assignment);
    free(assignment);

    if (ret < 0) {
        kywc_log(KYWC_WARN, "Failed to update systemd user environment: %s", strerror(-ret));
        return false;
    }

    return true;
}

void dbus_notify(const char *name, const char *summary, const char *body, const char *icon)
{
    if (!context || !context->user) {
        return;
    }

    sd_bus_call_method_async(context->user, NULL, "org.freedesktop.Notifications",
                             "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
                             "Notify", NULL, NULL, "susssasa{sv}i", name, 0, icon ? icon : "",
                             summary, body, 0, 0, 5000);
}

int dbus_add_match(const char *match, sd_bus_message_handler_t callback, void *data)
{
    if (!context || !context->user) {
        return false;
    }

    int ret = sd_bus_add_match(context->user, NULL, match, callback, data);
    if (ret < 0) {
        kywc_log(KYWC_ERROR, "Failed to add user match: %s", match);
        return false;
    }

    return true;
}

bool dbus_match_signal(const char *sender, const char *path, const char *interface,
                       const char *member, sd_bus_message_handler_t callback, void *data)
{
    if (!context || !context->user) {
        return false;
    }

    int ret =
        sd_bus_match_signal(context->user, NULL, sender, path, interface, member, callback, data);
    if (ret < 0) {
        kywc_log(KYWC_ERROR, "Failed to add user signal match: %s %s", interface, member);
        return false;
    }

    return true;
}

bool dbus_match_system_signal(const char *sender, const char *path, const char *interface,
                              const char *member, sd_bus_message_handler_t callback, void *data)
{
    if (!context || !context->system) {
        return false;
    }

    int ret =
        sd_bus_match_signal(context->system, NULL, sender, path, interface, member, callback, data);
    if (ret < 0) {
        kywc_log(KYWC_ERROR, "Failed to add system signal match: %s %s", interface, member);
        return false;
    }

    return true;
}

void dbus_emit_signal(const char *path, const char *interface, const char *member,
                      const char *types, ...)
{
    if (!context || !context->user) {
        return;
    }

    va_list args;
    va_start(args, types);
    sd_bus_emit_signalv(context->user, path, interface, member, types, args);
    va_end(args);
}
