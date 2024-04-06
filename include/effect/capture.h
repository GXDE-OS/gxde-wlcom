// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECT_CAPTURE_H_
#define _EFFECT_CAPTURE_H_

#include <stdbool.h>
#include <stdint.h>

struct wlr_buffer;
struct wlr_box;

typedef void (*capture_done_func_t)(struct wlr_buffer *buffer, int width, int height, void *data);

bool capture_area(struct wlr_box *rect, bool unscaled, bool cursor, capture_done_func_t done,
                  void *data);

bool capture_output(const char *name, bool unscaled, bool cursor, capture_done_func_t done,
                    void *data);

bool capture_fullscreen(bool unscaled, bool cursor, capture_done_func_t done, void *data);

void capture_read_buffer(struct wlr_buffer *buffer, uint32_t format, uint32_t stride,
                         struct wlr_box *box, void *data);

void capture_write_file(struct wlr_buffer *buffer, int width, int height, const char *path,
                        void (*done)(const char *path, void *data), void *user_data);

#endif /* _EFFECT_CAPTURE_H_ */
