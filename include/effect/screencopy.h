// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECT_SCREENCOPY_H_
#define _EFFECT_SCREENCOPY_H_

#include <stdbool.h>
#include <stdint.h>

struct wlr_buffer;
struct wlr_box;

typedef bool (*screencopy_done_func_t)(struct wlr_buffer *buffer, int width, int height,
                                       void *data);

bool screencopy_area(struct wlr_box *rect, bool unscaled, bool cursor, screencopy_done_func_t done,
                     void *data);

bool screencopy_output(const char *name, bool unscaled, bool cursor, screencopy_done_func_t done,
                       void *data);

bool screencopy_full(bool unscaled, bool cursor, screencopy_done_func_t done, void *data);

void screencopy_read_buffer(struct wlr_buffer *buffer, uint32_t format, uint32_t stride,
                            struct wlr_box *box, void *data);

#endif /* _EFFECT_SCREENCOPY_H_ */
