// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _SCALED_BUFFER_H_
#define _SCALED_BUFFER_H_

#include "scene/scene.h"

typedef void (*scaled_buffer_update_func_t)(struct ky_scene_buffer *buffer, float scale,
                                            void *data);
typedef void (*scaled_buffer_destroy_func_t)(struct ky_scene_buffer *buffer, void *data);

struct ky_scene_buffer *scaled_buffer_create(struct ky_scene_tree *parent, float scale,
                                             scaled_buffer_update_func_t update,
                                             scaled_buffer_destroy_func_t destroy, void *data);

#endif /* _SCALED_BUFFER_H_ */
