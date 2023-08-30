// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _SCENE_XDG_SHELL_H_
#define _SCENE_XDG_SHELL_H_

#include "scene.h"
#include "surface.h"

struct wlr_xdg_surface;

struct ky_scene_tree *ky_scene_xdg_surface_create(struct ky_scene_tree *parent,
                                                  struct wlr_xdg_surface *xdg_surface);

#endif /* _SCENE_XDG_SHELL_H_ */
