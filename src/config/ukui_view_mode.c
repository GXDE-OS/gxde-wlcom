// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <kywc/log.h>

#include "config_p.h"
#include "util/dbus.h"
#include "view/view.h"

static int handle_mode_change_signal(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    int tablet_mode = 0;
    int ret = sd_bus_message_read(m, "b", &tablet_mode);
    if (ret < 0) {
        kywc_log(KYWC_WARN, "Failed to parse D-Bus response for mode change: %s", strerror(-ret));
        return 0;
    }

    const char *name = tablet_mode ? "tablet_mode" : "stack_mode";
    view_manager_set_view_mode(name);
    return 0;
}

bool ukui_view_mode_manager_create(struct config_manager *config_manager)
{
    if (!dbus_match_signal("com.kylin.statusmanager.interface", "/",
                           "com.kylin.statusmanager.interface", "mode_change_signal",
                           handle_mode_change_signal, NULL)) {
        kywc_log(KYWC_ERROR, "ukui_view_mode_manager match mode_change_signal error");
        return false;
    }

    if (!dbus_call_method("com.kylin.statusmanager.interface", "/",
                          "com.kylin.statusmanager.interface", "get_current_tabletmode",
                          handle_mode_change_signal, NULL)) {
        kywc_log(KYWC_ERROR, "ukui_view_mode_manager get current mode error");
        return false;
    }

    return true;
}
