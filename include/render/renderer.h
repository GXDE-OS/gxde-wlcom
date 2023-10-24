// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _RENDERER_H_
#define _RENDERER_H_

struct wlr_backend;

struct wlr_renderer *ky_renderer_autocreate(struct wlr_backend *backend);

#endif /* _RENDER_H_ */
