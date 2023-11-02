// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _DEFAULT_SOURCE
#include <limits.h>
#include <stdlib.h>

#include <kywc/log.h>

#include "config_p.h"
#include "effect/screencopy.h"
#include "painter.h"
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

static void screenshot_write_image(struct wlr_buffer *buffer)
{
    char path[PATH_MAX];
    snprintf(path, PATH_MAX, "/tmp/%s", "kywc_screenshot_XXXXXX.png");
    mkstemps(path, 4);

    painter_buffer_to_file(buffer, path);
    wlr_buffer_drop(buffer);

    sd_bus_reply_method_return(manager->msg, "s", path);
    sd_bus_message_unref(manager->msg);
    manager->msg = NULL;

    manager->taking_screenshot = false;
    kywc_log(KYWC_DEBUG, "screenshot done, send reply %s", path);
}

static void write_image(void *job, void *gdata, int index)
{
    struct wlr_buffer *buffer = job;
    kywc_log(KYWC_DEBUG, "%s: in thread %d", __func__, index);
    screenshot_write_image(buffer);
}

static bool screenshot_done(struct wlr_buffer *buffer, int width, int height, void *data)
{
    uint32_t format;
    size_t stride;
    void *dst_ptr;

    struct wlr_buffer *dst_buf = painter_create_buffer(width, height, 1.0);
    wlr_buffer_begin_data_ptr_access(dst_buf, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &dst_ptr, &format,
                                     &stride);
    screencopy_read_buffer(buffer, format, stride,
                           &(struct wlr_box){ 0, 0, dst_buf->width, dst_buf->height }, dst_ptr);
    wlr_buffer_end_data_ptr_access(dst_buf);
    kywc_log(KYWC_DEBUG, "screenshot copy buffer to memory");

    if (!queue_add_job(&manager->server->queue, dst_buf, write_image, NULL)) {
        screenshot_write_image(dst_buf);
    }

    return true;
}

static int screenshot_fullscreen(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    if (manager->taking_screenshot) {
        const sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(
            "org.ukui.kwin.Screenshot.Error.AlreadyTaking", "A screenshot is already been taken");
        return sd_bus_reply_method_error(msg, &error);
    }

    if (!screencopy_full(false, false, screenshot_done, NULL)) {
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

    if (!screencopy_full(unscaled, cursor, screenshot_done, NULL)) {
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

    if (!screencopy_output(name, unscaled, cursor, screenshot_done, NULL)) {
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

    if (!screencopy_area(&(struct wlr_box){ x, y, width, height }, unscaled, cursor,
                         screenshot_done, NULL)) {
        return 0;
    }

    manager->msg = sd_bus_message_ref(msg);
    manager->taking_screenshot = true;

    return 1;
}

static const sd_bus_vtable screenshot_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("screenshotFullscreen", "", "s", screenshot_fullscreen, 0),
    SD_BUS_METHOD("screenshotFull", "bb", "s", screenshot_full, 0),
    SD_BUS_METHOD("screenshotOutput", "sbb", "s", screenshot_output, 0),
    SD_BUS_METHOD("screenshotArea", "iiiibb", "s", screenshot_area, 0),
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

    manager->server = config_manager->server;
    manager->destroy.notify = handle_config_destroy;
    wl_signal_add(&config->events.destroy, &manager->destroy);

    return true;
}
