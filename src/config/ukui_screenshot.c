// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <wlr/types/wlr_buffer.h>
#include <wlr/util/box.h>

#include <kywc/identifier.h>
#include <kywc/log.h>

#include "config_p.h"
#include "effect/capture.h"
#include "server.h"

static const char *registry_bus = "org.ukui.KWin";
static const char *registry_path = "/Screenshot";
static const char *registry_interface = "org.ukui.kwin.Screenshot";

static struct screenshot_manager {
    struct server *server;
    struct wl_listener destroy;

    sd_bus_message *msg;
    bool taking_screenshot;
} *manager = NULL;

static void screenshot_finish(const char *path, void *data)
{
    sd_bus_reply_method_return(manager->msg, "s", path);
    sd_bus_message_unref(manager->msg);
    manager->msg = NULL;

    manager->taking_screenshot = false;
    kywc_log(KYWC_DEBUG, "screenshot done, send reply %s", path);
}

static void screenshot_done(struct wlr_buffer *buffer, int width, int height, void *data)
{
    char path[32];
    snprintf(path, 32, "/tmp/%s", "kywc_screenshot_XXXXXX.png");
    kywc_identifier_rand_generate(path, 4);

    capture_write_file(buffer, width, height, path, screenshot_finish, NULL);
}

static int screenshot_fullscreen(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    if (manager->taking_screenshot) {
        const sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(
            "org.ukui.kwin.Screenshot.Error.AlreadyTaking", "A screenshot is already been taken");
        return sd_bus_reply_method_error(msg, &error);
    }

    if (!capture_fullscreen(false, false, screenshot_done, NULL)) {
        return 0;
    }

    manager->msg = sd_bus_message_ref(msg);
    manager->taking_screenshot = true;

    return 1;
}

static int screenshot2_fullscreen(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    if (manager->taking_screenshot) {
        const sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(
            "org.kde.kwin.Screenshot.Error.AlreadyTaking", "A screenshot is already been taken");
        return sd_bus_reply_method_error(msg, &error);
    }

    uint32_t cursor = 0;
    CK(sd_bus_message_read(msg, "b", &cursor));

    if (!capture_fullscreen(false, cursor, screenshot_done, NULL)) {
        return 0;
    }

    manager->msg = sd_bus_message_ref(msg);
    manager->taking_screenshot = true;

    return 1;
}

static int screenshot_full(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    if (manager->taking_screenshot) {
        const sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(
            "org.ukui.kwin.Screenshot.Error.AlreadyTaking", "A screenshot is already been taken");
        return sd_bus_reply_method_error(msg, &error);
    }

    uint32_t unscaled, cursor;
    CK(sd_bus_message_read(msg, "bb", &unscaled, &cursor));

    if (!capture_fullscreen(unscaled, cursor, screenshot_done, NULL)) {
        return 0;
    }

    manager->msg = sd_bus_message_ref(msg);
    manager->taking_screenshot = true;

    return 1;
}

static int screenshot_output(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    if (manager->taking_screenshot) {
        const sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(
            "org.ukui.kwin.Screenshot.Error.AlreadyTaking", "A screenshot is already been taken");
        return sd_bus_reply_method_error(msg, &error);
    }

    const char *name = NULL;
    uint32_t unscaled, cursor;
    CK(sd_bus_message_read(msg, "sbb", &name, &unscaled, &cursor));

    if (!capture_output(name, unscaled, cursor, screenshot_done, NULL)) {
        return 0;
    }

    manager->msg = sd_bus_message_ref(msg);
    manager->taking_screenshot = true;

    return 1;
}

static int screenshot_area(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    if (manager->taking_screenshot) {
        const sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(
            "org.ukui.kwin.Screenshot.Error.AlreadyTaking", "A screenshot is already been taken");
        return sd_bus_reply_method_error(msg, &error);
    }

    int x, y, width, height;
    uint32_t unscaled, cursor;
    CK(sd_bus_message_read(msg, "iiiibb", &x, &y, &width, &height, &unscaled, &cursor));

    if (!capture_area(&(struct wlr_box){ x, y, width, height }, unscaled, cursor, screenshot_done,
                      NULL)) {
        return 0;
    }

    manager->msg = sd_bus_message_ref(msg);
    manager->taking_screenshot = true;

    return 1;
}

/**
 * sd-bus not support method overloaded, https://github.com/systemd/systemd/issues/578
 * Add org.kde.KWin screenshotFullscreen with a bool arg for linuxqq,
 * keep org.ukui.KWin screenshotFullscreen without args for kylin-screenshot.
 */
static const sd_bus_vtable screenshot_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("screenshotFullscreen", "", "s", screenshot_fullscreen, 0),
    SD_BUS_METHOD("screenshotFull", "bb", "s", screenshot_full, 0),
    SD_BUS_METHOD("screenshotOutput", "sbb", "s", screenshot_output, 0),
    SD_BUS_METHOD("screenshotArea", "iiiibb", "s", screenshot_area, 0),
    SD_BUS_VTABLE_END,
};

static const sd_bus_vtable screenshot2_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("screenshotFullscreen", "b", "s", screenshot2_fullscreen, 0),
    SD_BUS_VTABLE_END,
};

static void handle_config_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->destroy.link);
    free(manager);
    manager = NULL;
}

bool ukui_screenshot_create(struct config_manager *config_manager)
{
    manager = calloc(1, sizeof(struct screenshot_manager));
    if (!manager) {
        return false;
    }

    struct config *config = config_manager_add_config(NULL, registry_bus, registry_path,
                                                      registry_interface, screenshot_vtable, NULL);
    if (!config) {
        free(manager);
        manager = NULL;
        return false;
    }

    config_manager_add_config(NULL, "org.kde.KWin", registry_path, "org.kde.kwin.Screenshot",
                              screenshot2_vtable, NULL);

    manager->server = config_manager->server;
    manager->destroy.notify = handle_config_destroy;
    wl_signal_add(&config->events.destroy, &manager->destroy);

    return true;
}
