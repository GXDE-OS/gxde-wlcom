// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _NODE_EFFECT_TRANSFORM_H_
#define _NODE_EFFECT_TRANSFORM_H_

#include "effect/transform.h"

bool node_add_transform_effect(struct ky_scene_node *node, struct transform_options *options);

bool node_transform_effect_create(struct effect_manager *manager);

#endif /* _NODE_EFFECT_TRANSFORM_H_ */