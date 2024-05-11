// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECT_CAPTURE_H_
#define _EFFECT_CAPTURE_H_

#include "output.h"

struct capture_update_event {
    struct wlr_buffer *buffer;
    bool buffer_changed;
};

enum capture_option {
    CAPTURE_NEED_NONE = 0,
    /* use buffer coord */
    CAPTURE_NEED_UNSCALED = 1 << 0,
    /**
     * embed cursor into buffer.
     * there is no guarantee the cursor will be embeded or not,
     * caused by hardware cursor is not support or
     * other capture request is different in cursor option.
     */
    CAPTURE_NEED_CURSOR = 1 << 1,
};

struct capture *capture_create_from_output(struct output *output, uint32_t options);

struct capture *capture_create_from_area(struct wlr_box *rect, uint32_t options);

struct capture *capture_create_from_fullscreen(uint32_t options);

void capture_add_update_listener(struct capture *capture, struct wl_listener *listener);

void capture_add_destroy_listener(struct capture *capture, struct wl_listener *listener);

void capture_mark_wants_update(struct capture *capture, bool wants, bool force);

void capture_destroy(struct capture *capture);

void capture_read_buffer(struct wlr_buffer *buffer, uint32_t format, uint32_t stride,
                         struct wlr_box *box, void *data);

void capture_write_file(struct wlr_buffer *buffer, int width, int height, const char *path,
                        void (*done)(const char *path, void *data), void *user_data);

#endif /* _EFFECT_CAPTURE_H_ */
