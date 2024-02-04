// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _UTIL_MATRIX_H_
#define _UTIL_MATRIX_H_

#include <wayland-server-protocol.h>

// Column Major (base glsl)
struct ky_mat3 {
    float matrix[9];
};

void ky_mat3_identity(struct ky_mat3 *mat3);

// Right Multiplication
void ky_mat3_multiply(struct ky_mat3 *a_mat, struct ky_mat3 *b_mat, struct ky_mat3 *result);

void ky_mat3_translate(struct ky_mat3 *mat3, float x, float y);

void ky_mat3_scale(struct ky_mat3 *mat3, float x, float y);

// counter clock wise
void ky_mat3_rotate(struct ky_mat3 *mat3, float rad);

void ky_mat3_flip_x(struct ky_mat3 *mat3);

void ky_mat3_flip_y(struct ky_mat3 *mat3);

// use framebuffer coord not logic coord. see ky_scene_render_box
void ky_mat3_framebuffer_to_ndc(struct ky_mat3 *mat3, int width, int height);

/* not include translate & scale
    // example
    struct ky_mat3 transform;
    ky_mat3_identity(&transform);

    // keep scale->rotate->translate order
    ky_mat3_scale(&transform, target->scale, target->scale);
    // ky_mat3_rotate(&transform, 3.14 * 0.25); // if need rotate
    ky_mat3_translate(&transform, lx - target->logical.x, ly - target->logical.y);

    struct ky_mat3 to_ndc;
    ky_mat3_logic_to_ndc(&to_ndc, target->logical.width, target->logical.height, target->transform);

    struct ky_mat3 transform_ndc;
    ky_mat3_multiply(&to_ndc, &transform, &transform_ndc);

    // vertex shader
    gl_Position = vec4(transform_ndc * vec3(position, 1.0), 1.0);
*/
void ky_mat3_logic_to_ndc(struct ky_mat3 *mat3, int width, int height,
                          enum wl_output_transform transform);

#endif /* _UTIL_MATRIX_H_ */
