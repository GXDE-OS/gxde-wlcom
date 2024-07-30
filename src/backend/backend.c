// SPDX-FileCopyrightText: 2024 The wlroots contributors
// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <libudev.h>
#include <string.h>

#include <kywc/log.h>

#include <wlr/backend/libinput.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/session.h>

#include "backend/backend.h"
#include "backend/fbdev.h"
#include "util/time.h"

#define WAIT_SESSION_TIMEOUT 10000 // ms
#define WAIT_GPU_TIMEOUT 1500      // ms
#define MAX_NUM_GPUS 8

static struct wlr_session *session_create_and_wait(struct wl_display *disp)
{
    struct wlr_session *session = wlr_session_create(disp);
    if (!session) {
        kywc_log(KYWC_ERROR, "Failed to start a session");
        return NULL;
    }

    if (!session->active) {
        kywc_log(KYWC_INFO, "Waiting for a session to become active");

        int64_t started_at = current_time_msec();
        int64_t timeout = WAIT_SESSION_TIMEOUT;
        struct wl_event_loop *event_loop = wl_display_get_event_loop(session->display);

        while (!session->active) {
            int ret = wl_event_loop_dispatch(event_loop, (int)timeout);
            if (ret < 0) {
                kywc_log_errno(KYWC_ERROR, "Failed to wait for session active: "
                                           "wl_event_loop_dispatch failed");
                return NULL;
            }

            int64_t now = current_time_msec();
            if (now >= started_at + WAIT_SESSION_TIMEOUT) {
                break;
            }
            timeout = started_at + WAIT_SESSION_TIMEOUT - now;
        }

        if (!session->active) {
            kywc_log(KYWC_ERROR, "Timeout waiting session to become active");
            return NULL;
        }
    }

    return session;
}

static int explicit_find_fbs(struct wlr_session *session, int dev_len, char *dev[static dev_len],
                             const char *str)
{
    char *fbs = strdup(str);
    if (!fbs) {
        kywc_log(KYWC_ERROR, "Allocation failed");
        return -1;
    }

    int i = 0;
    char *save;
    char *ptr = strtok_r(fbs, ":", &save);
    do {
        if (i >= dev_len) {
            break;
        }

        dev[i] = strdup(ptr);
        if (!dev[i]) {
            kywc_log(KYWC_ERROR, "Allocation failed");
            break;
        }

        ++i;
    } while ((ptr = strtok_r(NULL, ":", &save)));

    free(fbs);
    return i;
}

static int session_find_framebuffer_device(struct wlr_session *session, char **devices,
                                           int devices_len)
{
    const char *explicit = getenv("KYWC_FB_DEVICES");
    if (explicit) {
        return explicit_find_fbs(session, devices_len, devices, explicit);
    }

    struct udev_enumerate *enumerate = udev_enumerate_new(session->udev);
    if (!enumerate) {
        return -1;
    }

    udev_enumerate_add_match_sysname(enumerate, "fb[0-9]*");
    if (udev_enumerate_add_match_subsystem(enumerate, "graphics") < 0) {
        udev_enumerate_unref(enumerate);
        kywc_log(KYWC_ERROR, "Failed to add match subsystem");
        return -1;
    }

    if (udev_enumerate_scan_devices(enumerate) < 0) {
        udev_enumerate_unref(enumerate);
        kywc_log(KYWC_ERROR, "Failed to scan devices");
        return -1;
    }

    int i = 0;
    struct udev_list_entry *entry;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate)) {
        if (i >= devices_len) {
            break;
        }

        const char *path = udev_list_entry_get_name(entry);
        struct udev_device *device = udev_device_new_from_syspath(session->udev, path);
        if (!device) {
            continue;
        }

        const char *seat = udev_device_get_property_value(device, "ID_SEAT");
        if (!seat) {
            seat = "seat0";
        }
        if (session->seat[0] && strcmp(session->seat, seat) != 0) {
            udev_device_unref(device);
            continue;
        }

        /* a framebuffer device was found */
        char *fbdev_path = strdup(udev_device_get_devnode(device));
        udev_device_unref(device);
        if (!fbdev_path) {
            kywc_log(KYWC_ERROR, "Allocation failed");
            break;
        }
        devices[i++] = fbdev_path;
    }

    udev_enumerate_unref(enumerate);

    return i;
}

static void frambffer_devices_release(char **devices, int n)
{
    for (int i = 0; i < n; i++) {
        free((void *)devices[i]);
    }
}

static bool attempt_fbdev_backend(struct wl_display *display, struct wlr_backend *backend,
                                  struct wlr_session *session)
{
    char *devices[MAX_NUM_GPUS];
    int n = session_find_framebuffer_device(session, devices, MAX_NUM_GPUS);
    if (n <= 0) {
        kywc_log(KYWC_ERROR, "Failed to find framebuffer device, can not create backend");
        return false;
    }

    kywc_log(KYWC_INFO, "Found %d framebuffer devices", n);

    struct wlr_backend *fbdev = fbdev_backend_create(display, session, (const char **)devices, n);
    if (!fbdev) {
        kywc_log(KYWC_ERROR, "Failed to create fbdev backend");
        frambffer_devices_release(devices, n);
        return false;
    }

    wlr_multi_backend_add(backend, fbdev);
    frambffer_devices_release(devices, n);

    return true;
}

static struct wlr_backend *ky_fbdev_backend_create(struct wl_display *display,
                                                   struct wlr_session **session_ptr)
{
    if (session_ptr != NULL) {
        *session_ptr = NULL;
    }

    struct wlr_session *session = NULL;
    struct wlr_backend *multi = wlr_multi_backend_create(display);
    if (!multi) {
        kywc_log(KYWC_ERROR, "Could not allocate multibackend");
        return NULL;
    }

    // Attempt fbdev+libinput
    session = session_create_and_wait(display);
    if (!session) {
        kywc_log(KYWC_ERROR, "Failed to start a framebuffer session");
        goto error;
    }

    if (!attempt_fbdev_backend(display, multi, session)) {
        kywc_log(KYWC_ERROR, "Failed to open any framebuffer device");
        goto error;
    }

    struct wlr_backend *libinput = wlr_libinput_backend_create(display, session);
    if (libinput) {
        wlr_multi_backend_add(multi, libinput);
    } else {
        kywc_log(KYWC_ERROR, "Failed to start libinput backend");
        goto error;
    }

    if (session_ptr != NULL) {
        *session_ptr = session;
    }

    return multi;

error:
    wlr_backend_destroy(multi);
    wlr_session_destroy(session);
    return NULL;
}

static bool find_drm_cards(struct wl_display *display)
{
    struct udev *udev = udev_new();
    if (!udev) {
        kywc_log_errno(KYWC_ERROR, "Failed to create udev context");
        return false;
    }

    bool found = false;
    struct udev_enumerate *en = udev_enumerate_new(udev);
    if (!en) {
        kywc_log(KYWC_ERROR, "udev_enumerate_new failed");
        goto out;
    }

    udev_enumerate_add_match_subsystem(en, "drm");
    udev_enumerate_add_match_sysname(en, "card[0-9]*");

    if (udev_enumerate_scan_devices(en) != 0) {
        kywc_log(KYWC_ERROR, "udev_enumerate_scan_devices failed");
        goto out;
    }

    if (udev_enumerate_get_list_entry(en) == NULL) {
        kywc_log(KYWC_INFO, "Waiting for a KMS device");

        int64_t started_at = current_time_msec();
        int64_t timeout = WAIT_GPU_TIMEOUT;
        struct wl_event_loop *event_loop = wl_display_get_event_loop(display);

        while (1) {
            int ret = wl_event_loop_dispatch(event_loop, (int)timeout);
            if (ret < 0) {
                kywc_log_errno(KYWC_ERROR, "wl_event_loop_dispatch failed");
                goto out;
            }
            int64_t now = current_time_msec();
            if (now >= started_at + WAIT_GPU_TIMEOUT) {
                break;
            }
            timeout = started_at + WAIT_GPU_TIMEOUT - now;
        }

        if (udev_enumerate_scan_devices(en) != 0) {
            kywc_log(KYWC_ERROR, "udev_enumerate_scan_devices failed");
            goto out;
        }

        if (udev_enumerate_get_list_entry(en) == NULL) {
            kywc_log(KYWC_INFO, "Found 0 GPUs, trying fbdev backend");
            goto out;
        }
    }

    found = true;

out:
    if (en) {
        udev_enumerate_unref(en);
    }
    udev_unref(udev);
    return found;
}

struct wlr_backend *ky_backend_autocreate(struct wl_display *display,
                                          struct wlr_session **session_ptr)
{
    const char *env = getenv("KYWC_BACKEND");
    if (env && strcmp(env, "fbdev") == 0) {
        return ky_fbdev_backend_create(display, session_ptr);
    }

    if (getenv("WAYLAND_DISPLAY") || getenv("WAYLAND_SOCKET") || getenv("DISPLAY")) {
        return wlr_backend_autocreate(display, session_ptr);
    }

    struct wlr_backend *backend = NULL;
    if (find_drm_cards(display)) {
        backend = wlr_backend_autocreate(display, session_ptr);
    }
    return backend ? backend : ky_fbdev_backend_create(display, session_ptr);
}
