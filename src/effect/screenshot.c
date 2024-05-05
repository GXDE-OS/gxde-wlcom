// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <wlr/types/wlr_buffer.h>
#include <wlr/util/box.h>

#include <kywc/identifier.h>
#include <kywc/log.h>

#include "config.h"
#include "effect/capture.h"
#include "effect_p.h"
#include "server.h"

static const char *registry_bus = "org.ukui.KWin";
static const char *registry_path = "/Screenshot";
static const char *registry_interface = "org.ukui.kwin.Screenshot";

static struct screenshot_manager {
    struct server *server;
    struct wl_listener destroy;

    struct capture *capture;
    struct wl_listener capture_update;
    struct wl_listener capture_destroy;

    sd_bus_message *msg;
    bool taking_screenshot;
} *manager = NULL;

static void screenshot_finish(const char *path, void *data)
{
    if (path) {
        sd_bus_reply_method_return(manager->msg, "s", path);
        kywc_log(KYWC_DEBUG, "screenshot done, send reply %s", path);
    } else {
        const sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(
            "org.ukui.kwin.Screenshot.Error.Cancelled", "Screenshot got cancelled");
        sd_bus_reply_method_error(manager->msg, &error);
    }

    sd_bus_message_unref(manager->msg);
    manager->msg = NULL;

    manager->taking_screenshot = false;
}

static void manager_destroy_capture(void)
{
    wl_list_remove(&manager->capture_destroy.link);
    wl_list_remove(&manager->capture_update.link);

    if (manager->capture) {
        capture_destroy(manager->capture);
        manager->capture = NULL;
    }
}

static void handle_capture_update(struct wl_listener *listener, void *data)
{
    struct capture_update_event *event = data;

    char path[32];
    snprintf(path, 32, "/tmp/%s", "kywc_screenshot_XXXXXX.png");
    kywc_identifier_rand_generate(path, 4);

    capture_write_file(event->buffer, event->buffer->width, event->buffer->height, path,
                       screenshot_finish, NULL);

    manager_destroy_capture();
}

static void handle_capture_destroy(struct wl_listener *listener, void *data)
{
    manager->capture = NULL;
    manager_destroy_capture();

    /* capture failed, send a error to client */
    screenshot_finish(NULL, NULL);
}

static int screenshot_fullscreen(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    if (manager->taking_screenshot) {
        const sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(
            "org.ukui.kwin.Screenshot.Error.AlreadyTaking", "A screenshot is already been taken");
        return sd_bus_reply_method_error(msg, &error);
    }

    manager->capture = capture_create_from_fullscreen(CAPTURE_NEED_NONE);
    if (!manager->capture) {
        return 0;
    }

    capture_add_update_listener(manager->capture, &manager->capture_update);
    capture_add_destroy_listener(manager->capture, &manager->capture_destroy);

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

    uint32_t options = cursor ? CAPTURE_NEED_CURSOR : CAPTURE_NEED_NONE;

    manager->capture = capture_create_from_fullscreen(options);
    if (!manager->capture) {
        return 0;
    }

    capture_add_update_listener(manager->capture, &manager->capture_update);
    capture_add_destroy_listener(manager->capture, &manager->capture_destroy);

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

    uint32_t options = unscaled ? CAPTURE_NEED_UNSCALED : CAPTURE_NEED_NONE;
    options |= cursor ? CAPTURE_NEED_CURSOR : CAPTURE_NEED_NONE;

    manager->capture = capture_create_from_fullscreen(options);
    if (!manager->capture) {
        return 0;
    }

    capture_add_update_listener(manager->capture, &manager->capture_update);
    capture_add_destroy_listener(manager->capture, &manager->capture_destroy);

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

    struct kywc_output *output = kywc_output_by_name(name);
    if (!output) {
        const sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(
            "org.ukui.kwin.Screenshot.Error.InvalidOutput", "Invalid output requested");
        return sd_bus_reply_method_error(msg, &error);
    }

    uint32_t options = unscaled ? CAPTURE_NEED_UNSCALED : CAPTURE_NEED_NONE;
    options |= cursor ? CAPTURE_NEED_CURSOR : CAPTURE_NEED_NONE;

    manager->capture = capture_create_from_output(output_from_kywc_output(output), options);
    if (!manager->capture) {
        return 0;
    }

    capture_add_update_listener(manager->capture, &manager->capture_update);
    capture_add_destroy_listener(manager->capture, &manager->capture_destroy);

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

    uint32_t options = unscaled ? CAPTURE_NEED_UNSCALED : CAPTURE_NEED_NONE;
    options |= cursor ? CAPTURE_NEED_CURSOR : CAPTURE_NEED_NONE;

    manager->capture = capture_create_from_area(&(struct wlr_box){ x, y, width, height }, options);
    if (!manager->capture) {
        return 0;
    }

    capture_add_update_listener(manager->capture, &manager->capture_update);
    capture_add_destroy_listener(manager->capture, &manager->capture_destroy);

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

bool screenshot_effect_create(struct effect_manager *effect_manager)
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

    manager->server = effect_manager->server;
    manager->destroy.notify = handle_config_destroy;
    wl_signal_add(&config->events.destroy, &manager->destroy);

    manager->capture_update.notify = handle_capture_update;
    manager->capture_destroy.notify = handle_capture_destroy;

    return true;
}
