/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>

#include <kywc/log.h>
#include <systemd/sd-bus.h>

#include "util/dbus.h"
#include "../../../view/view_p.h"

#define DEEPIN_WM_SERVICE "com.deepin.wm"
#define DEEPIN_WM_PATH "/com/deepin/wm"
#define DEEPIN_WM_INTERFACE "com.deepin.wm"
#define DEEPIN_WM_SHOW_WORKSPACE 1
#define DEEPIN_WM_SHOW_ALL_WINDOWS 2

static int perform_action(sd_bus_message* message, void* userdata,
        sd_bus_error* ret_error) {
    int32_t type;
    CK(sd_bus_message_read(message, "i", &type));

    switch (type) {
    case DEEPIN_WM_SHOW_WORKSPACE:
        if (!multitask_view_toggle()) {
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_FAILED,
                "(Multitask) Init: The multitask view is not initialized");
        }
        return sd_bus_reply_method_return(message, NULL);
    case DEEPIN_WM_SHOW_ALL_WINDOWS:
        if (!present_windows_toggle()) {
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_FAILED,
                "(PresentWindows) Init: The present windows view is not initialized");
        }
        return sd_bus_reply_method_return(message, NULL);
    default:
        return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_NOT_SUPPORTED,
            "(Multitask) Init: Action %d is not supported", type);
    }
}

static int get_is_show_desktop(sd_bus_message* message, void* userdata,
        sd_bus_error* ret_error) {
    return sd_bus_reply_method_return(message, "b",
        view_manager_get_show_desktop());
}

static int set_show_desktop(sd_bus_message* message, void* userdata,
        sd_bus_error* ret_error) {
    int enabled;
    CK(sd_bus_message_read(message, "b", &enabled));
    view_manager_show_desktop(enabled, true);
    return sd_bus_reply_method_return(message, NULL);
}

static const sd_bus_vtable deepin_wm_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("PerformAction", "i", "", perform_action, 0),
    SD_BUS_METHOD("GetIsShowDesktop", "", "b", get_is_show_desktop, 0),
    SD_BUS_METHOD("SetShowDesktop", "b", "", set_show_desktop, 0),
    SD_BUS_VTABLE_END,
};

bool multitask_launcher_interface_create(void) {
    if (!dbus_register_object(DEEPIN_WM_SERVICE, DEEPIN_WM_PATH,
            DEEPIN_WM_INTERFACE,
            deepin_wm_vtable, NULL)) {
        kywc_log(KYWC_ERROR,
            "(Multitask) DBus: Failed to register the %s multitask launcher interface",
            DEEPIN_WM_SERVICE);
        return false;
    }

    kywc_log(KYWC_INFO,
        "(Multitask) DBus: Registered multitask launcher interface %s at %s",
        DEEPIN_WM_INTERFACE, DEEPIN_WM_PATH);
    return true;
}
