// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _CLIENT_BUFFER_H_
#define _CLIENT_BUFFER_H_

#include <libkywc.h>

struct kywc_buffer_helper *kywc_buffer_helper_create(kywc_context *ctx);

void kywc_buffer_helper_destroy(struct kywc_buffer_helper *helper);

struct kywc_buffer *kywc_buffer_helper_import_thumbnail(struct kywc_buffer_helper *helper,
                                                        kywc_thumbnail *thumbnail,
                                                        const struct kywc_thumbnail_buffer *buffer);

bool kywc_buffer_write_to_file(struct kywc_buffer *buffer, const char *path);

bool kywc_buffer_show_in_window(struct kywc_buffer *buffer, const char *title);

void kywc_buffer_destroy(struct kywc_buffer *buffer);

#endif /* _CLIENT_BUFFER_H_ */
