// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <systemd/sd-bus.h>
#include <wayland-server-core.h>

#define CK(v)                                                                                      \
    do {                                                                                           \
        int tmp = (v);                                                                             \
        if (tmp < 0)                                                                               \
            return tmp;                                                                            \
    } while (0)

struct server;

bool dbus_context_create(struct server *server);

struct dbus_object *dbus_register_object(const char *name, const char *path, const char *interface,
                                         const sd_bus_vtable *vtable, void *data);

void dbus_unregister_object(struct dbus_object *object);

bool dbus_call_method(const char *service, const char *path, const char *interface,
                      const char *method, sd_bus_message_handler_t callback, void *data);

bool dbus_call_system_method(const char *service, const char *path, const char *interface,
                             const char *method, sd_bus_message_handler_t callback, void *data);

/* Call a method taking a single string argument (fire-and-forget). */
bool dbus_call_method_str(const char *service, const char *path, const char *interface,
                          const char *method, const char *arg);

/* Fire-and-forget call of a method that takes three string arguments. */
bool dbus_call_method_str3(const char *service, const char *path, const char *interface,
                           const char *method, const char *arg1, const char *arg2,
                           const char *arg3);

bool dbus_update_activation_environment(const char *name, const char *value);

void dbus_notify(const char *name, const char *summary, const char *body, const char *icon);

int dbus_add_match(const char *match, sd_bus_message_handler_t callback, void *data);

bool dbus_match_signal(const char *sender, const char *path, const char *interface,
                       const char *member, sd_bus_message_handler_t callback, void *data);

bool dbus_match_system_signal(const char *sender, const char *path, const char *interface,
                              const char *member, sd_bus_message_handler_t callback, void *data);

void dbus_emit_signal(const char *path, const char *interface, const char *member,
                      const char *types, ...);
