// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _KYCOM_RECT_H_
#define _KYCOM_RECT_H_

#include <pixman.h>

#include "scene.h"

void kywc_rect_node_init(struct kywc_rect_node *rect_node, int width, int height,
                         const float color[static 4]);

struct kywc_rect_node *kywc_scene_rect_from_node(struct kywc_node *node);

struct kywc_rect_node *kywc_rect_node_create(struct kywc_group_node *parent, int width, int height,
                                             const float color[static 4]);

void kywc_rect_node_set_size(struct kywc_rect_node *rect, int width, int height);

void kywc_rect_node_set_color(struct kywc_rect_node *rect, const float color[static 4]);

#endif
