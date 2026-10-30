// SPDX-FileCopyrightText: 2026 GXDE OS Team
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>

struct view_manager;

void portal_create(struct view_manager *view_manager);

/* Called from the D-Bus layer. */
bool portal_begin(int x, int y, int w, int h);
void portal_cancel(void);

/* D-Bus plumbing, implemented in portal_dbus.c. */
bool portal_dbus_create(void);
void portal_dbus_destroy(void);
void portal_emit_drag_started(const char *mime_types);
void portal_emit_drag_ended(void);
void portal_emit_portal_closed(void);
void portal_emit_fullscreen_changed(bool fullscreen);
void portal_emit_dropped(const char *uuid, bool accepted);

/* Accessed by the D-Bus introspect/list implementation. */
struct view_manager *portal_view_manager(void);
