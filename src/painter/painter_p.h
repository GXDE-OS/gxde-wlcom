// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _PAINTER_P_H_
#define _PAINTER_P_H_

#include <wlr/types/wlr_buffer.h>

#include <kywc/log.h>

#include "painter.h"

struct painter_buffer {
    struct wlr_buffer base;
    size_t stride;
    float scale;

    int dst_width;
    int dst_height;

    void *data;
    bool own_data;
};

bool render_buffer(struct painter_buffer *buffer, struct draw_info *info);

void text_get_size(const char *font, int font_size, const char *text, int *width, int *height);

uint8_t *image_read_from_file(const char *filename, int *width, int *height);

bool image_write_to_file(struct painter_buffer *buffer, const char *filename);

bool image_write_to_memory(struct painter_buffer *buffer, char **data, size_t *size);

#endif /* _PAINTER_P_H_ */
