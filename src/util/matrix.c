// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <math.h>
#include <string.h>

#include "util/matrix.h"

#define M_PI (3.14159265358979323846)

void ky_mat3_identity(struct ky_mat3 *mat3)
{
    static const float identity[9] = {
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
    memcpy(mat3, identity, sizeof(identity));
}

void ky_mat3_init_scale_translate(struct ky_mat3 *mat3, float sx, float sy, float tx, float ty)
{
    mat3->matrix[0] = sx;
    mat3->matrix[1] = 0.f;
    mat3->matrix[2] = 0.f;
    mat3->matrix[3] = 0.f;
    mat3->matrix[4] = sy;
    mat3->matrix[5] = 0.f;
    mat3->matrix[6] = tx;
    mat3->matrix[7] = ty;
    mat3->matrix[8] = 1.f;
}

void ky_mat3_multiply(struct ky_mat3 *a_mat, struct ky_mat3 *b_mat, struct ky_mat3 *result)
{
    float *r = result->matrix;
    float *a = a_mat->matrix;
    float *b = b_mat->matrix;

    r[0] = a[0] * b[0] + a[3] * b[1] + a[6] * b[2];
    r[1] = a[1] * b[0] + a[4] * b[1] + a[7] * b[2];
    r[2] = a[2] * b[0] + a[5] * b[1] + a[8] * b[2];

    r[3] = a[0] * b[3] + a[3] * b[4] + a[6] * b[5];
    r[4] = a[1] * b[3] + a[4] * b[4] + a[7] * b[5];
    r[5] = a[2] * b[3] + a[5] * b[4] + a[8] * b[5];

    r[6] = a[0] * b[6] + a[3] * b[7] + a[6] * b[8];
    r[7] = a[1] * b[6] + a[4] * b[7] + a[7] * b[8];
    r[8] = a[2] * b[6] + a[5] * b[7] + a[8] * b[8];
}

void ky_mat3_translate(struct ky_mat3 *mat3, float x, float y)
{
    struct ky_mat3 translate;
    ky_mat3_identity(&translate);
    translate.matrix[6] = x;
    translate.matrix[7] = y;

    struct ky_mat3 mat = *mat3;
    ky_mat3_multiply(&translate, &mat, mat3);
}

void ky_mat3_scale(struct ky_mat3 *mat3, float x, float y)
{
    struct ky_mat3 scale;
    ky_mat3_identity(&scale);
    scale.matrix[0] = x;
    scale.matrix[4] = y;

    struct ky_mat3 mat = *mat3;
    ky_mat3_multiply(&scale, &mat, mat3);
}

void ky_mat3_rotate(struct ky_mat3 *mat3, float rad)
{
    struct ky_mat3 rotate;
    ky_mat3_identity(&rotate);
    rotate.matrix[0] = cosf(rad);
    rotate.matrix[1] = -sinf(rad);
    rotate.matrix[3] = sinf(rad);
    rotate.matrix[4] = cosf(rad);

    struct ky_mat3 mat = *mat3;
    ky_mat3_multiply(&rotate, &mat, mat3);
}

void ky_mat3_flip_x(struct ky_mat3 *mat3)
{
    struct ky_mat3 flip;
    ky_mat3_identity(&flip);
    flip.matrix[0] = -1;

    struct ky_mat3 mat = *mat3;
    ky_mat3_multiply(&flip, &mat, mat3);
}

void ky_mat3_flip_y(struct ky_mat3 *mat3)
{
    struct ky_mat3 flip;
    ky_mat3_identity(&flip);
    flip.matrix[4] = -1;

    struct ky_mat3 mat = *mat3;
    ky_mat3_multiply(&flip, &mat, mat3);
}

void ky_mat3_framebuffer_to_ndc(struct ky_mat3 *mat3, int width, int height)
{
    // ndc [-1 ~ 1]. 1/pixel * 2 - 1
    ky_mat3_identity(mat3);
    float *mat = mat3->matrix;
    // scale
    mat[0] = 1.0f / width * 2.0f;
    mat[4] = 1.0f / height * 2.0f;
    // translate
    mat[6] = -1.0f;
    mat[7] = -1.0f;
}

void ky_mat3_logic_to_ndc(struct ky_mat3 *mat, int width, int height,
                          enum wl_output_transform transform)
{
    // logic ndc [-1 ~ 1]
    ky_mat3_identity(mat);
    ky_mat3_scale(mat, 1.0f / width * 2.0f, 1.0f / height * 2.0f);
    ky_mat3_translate(mat, -1.0f, -1.0f);

    // flip and rotate
    switch (transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL:
        // do nothing
        break;
    case WL_OUTPUT_TRANSFORM_90:
        ky_mat3_rotate(mat, M_PI * 0.5f);
        break;
    case WL_OUTPUT_TRANSFORM_180:
        ky_mat3_rotate(mat, M_PI);
        break;
    case WL_OUTPUT_TRANSFORM_270:
        ky_mat3_rotate(mat, M_PI * 1.5f);
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED:
        ky_mat3_flip_x(mat);
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
        ky_mat3_flip_x(mat);
        ky_mat3_rotate(mat, M_PI * 0.5f);
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
        ky_mat3_flip_x(mat);
        ky_mat3_rotate(mat, M_PI);
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        ky_mat3_flip_x(mat);
        ky_mat3_rotate(mat, M_PI * 1.5f);
        break;
    }
}

void ky_mat3_invert_output_transform(struct ky_mat3 *mat, enum wl_output_transform transform)
{
    ky_mat3_identity(mat);
    // rotate center at (0,0)
    ky_mat3_translate(mat, -0.5, -0.5);
    switch (transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL:
        // do nothing
        break;
    case WL_OUTPUT_TRANSFORM_90:
        ky_mat3_rotate(mat, -M_PI * 0.5f);
        break;
    case WL_OUTPUT_TRANSFORM_180:
        ky_mat3_rotate(mat, -M_PI);
        break;
    case WL_OUTPUT_TRANSFORM_270:
        ky_mat3_rotate(mat, -M_PI * 1.5f);
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED:
        ky_mat3_flip_x(mat);
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
        ky_mat3_rotate(mat, -M_PI * 0.5f);
        ky_mat3_flip_x(mat);
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
        ky_mat3_rotate(mat, -M_PI);
        ky_mat3_flip_x(mat);
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        ky_mat3_rotate(mat, -M_PI * 1.5f);
        ky_mat3_flip_x(mat);
        break;
    }
    ky_mat3_translate(mat, 0.5, 0.5);
}

void ky_mat3_uvofbox_to_ndc(struct ky_mat3 *uv2ndc, int buffer_w, int buffer_h,
                            float rotation_angle, const struct kywc_box *dst_box)
{
    struct ky_mat3 projection;
    ky_mat3_framebuffer_to_ndc(&projection, buffer_w, buffer_h);

    struct ky_mat3 uv2pos;
    ky_mat3_identity(&uv2pos);
    ky_mat3_scale(&uv2pos, dst_box->width, dst_box->height);
    ky_mat3_translate(&uv2pos, dst_box->x, dst_box->y);
    ky_mat3_translate(&uv2pos, -buffer_w / 2.0, -buffer_h / 2.0);
    ky_mat3_rotate(&uv2pos, -rotation_angle * M_PI / 180);
    ky_mat3_translate(&uv2pos, buffer_w / 2.0, buffer_h / 2.0);
    ky_mat3_multiply(&projection, &uv2pos, uv2ndc);
}

void ky_mat3_uvofbox_to_texture(struct ky_mat3 *uv2tex, enum wl_output_transform trans,
                                const struct kywc_fbox *src_box)
{
    struct ky_mat3 tex_matrix;
    ky_mat3_identity(&tex_matrix);
    ky_mat3_scale(&tex_matrix, src_box->width, src_box->height);
    ky_mat3_translate(&tex_matrix, src_box->x, src_box->y);

    struct ky_mat3 rotation_m;
    ky_mat3_invert_output_transform(&rotation_m, trans);
    ky_mat3_multiply(&tex_matrix, &rotation_m, uv2tex);
}
