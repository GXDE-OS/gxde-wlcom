// SPDX-FileCopyrightText: 2024 The wlroots contributors
// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _FBDEV_H_
#define _FBDEV_H_

#include <backend/fbdev.h>
#include <linux/fb.h>
#include <wlr/interfaces/wlr_output.h>

struct fbdev_backend {
    struct wlr_backend backend;

    struct wl_display *display;
    struct wlr_session *session;

    struct wl_listener display_destroy;
    struct wl_listener session_destroy;
    struct wl_listener session_active;

    struct wl_list outputs;
};

struct fbdev_mode {
    struct wlr_output_mode wlr_mode;
    struct fb_var_screeninfo mode_info;
};

struct fbdev_screeninfo {
    uint32_t x_resolution; /* pixels, visible area */
    uint32_t y_resolution; /* pixels, visible area */
    uint32_t width_mm;     /* visible screen width in mm */
    uint32_t height_mm;    /* visible screen height in mm */
    uint32_t bits_per_pixel;

    uint32_t pixel_format; /* frame buffer pixel format */
    uint32_t refresh_rate; /* Hertz */

    size_t buffer_length; /* length of frame buffer memory in bytes */
    size_t line_length;   /* length of a line in bytes */

    struct fb_var_screeninfo current;

    char desc[16]; /* screen identifier */
};

struct fbdev_output {
    struct wlr_output wlr_output;

    struct fbdev_backend *backend;
    struct wl_list link;

    struct fbdev_mode mode; // builtin mode

    struct wl_event_source *frame_timer;
    int frame_delay; // ms

    struct fbdev_screeninfo screen_info;
    const char *device;

    int fd;
    bool dpms_mode_support;

    void *fb; // map
};

struct wlr_output *fbdev_output_create(struct wlr_backend *wlr_backend, const char *device);

struct fbdev_output *fbdev_output_from_output(struct wlr_output *wlr_output);

struct fbdev_backend *get_fbdev_backend_from_backend(struct wlr_backend *wlr_backend);

bool fbdev_output_reenable(struct fbdev_output *output);

bool fbdev_output_offscreen(struct fbdev_output *output);

#endif
