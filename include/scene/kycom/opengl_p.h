// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _KYCOM_OPENGL_P_H_
#define _KYCOM_OPENGL_P_H_

#include <wlr/render/wlr_renderer.h>

#include "kywc/kycom/opengl.h"

void _kywc_gl_init(struct wlr_renderer *renderer);

bool _kywc_gl_begin(uint32_t width, uint32_t height, uint32_t fb);

void _kywc_gl_end(uint32_t fb);

void _kywc_gl_update_current_framebuffer(void);

#endif
