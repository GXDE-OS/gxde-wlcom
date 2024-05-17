// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <math.h>

#include "util/vec2.h"

float ky_vec2_length2(struct ky_vec2 *vec)
{
    return vec->x * vec->x + vec->y * vec->y;
}

float ky_vec2_length(struct ky_vec2 *vec)
{
    return sqrtf(vec->x * vec->x + vec->y * vec->y);
}

float ky_vec2_distance2(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec)
{
    float x = b_vec->x - a_vec->x;
    float y = b_vec->y - a_vec->y;
    return x * x + y * y;
}

float ky_vec2_distance(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec)
{
    float x = b_vec->x - a_vec->x;
    float y = b_vec->y - a_vec->y;
    return sqrtf(x * x + y * y);
}

void ky_vec2_normalize(struct ky_vec2 *vec)
{
    float length = ky_vec2_length(vec);
    vec->x /= length;
    vec->y /= length;
}

void ky_vec2_normalize_to(struct ky_vec2 *vec, struct ky_vec2 *result)
{
    float length = ky_vec2_length(vec);
    result->x = vec->x / length;
    result->y = vec->y / length;
}

void ky_vec2_perpendicular(struct ky_vec2 *vec, struct ky_vec2 *result)
{
    result->x = vec->y;
    result->y = -vec->x;
}

void ky_vec2_add(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec, struct ky_vec2 *result)
{
    result->x = a_vec->x + b_vec->x;
    result->y = a_vec->y + b_vec->y;
}

void ky_vec2_sub(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec, struct ky_vec2 *result)
{
    result->x = a_vec->x - b_vec->x;
    result->y = a_vec->y - b_vec->y;
}

void ky_vec2_mul(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec, struct ky_vec2 *result)
{
    result->x = a_vec->x * b_vec->x;
    result->y = a_vec->y * b_vec->y;
}

void ky_vec2_div(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec, struct ky_vec2 *result)
{
    result->x = a_vec->x / b_vec->x;
    result->y = a_vec->y / b_vec->y;
}

void ky_vec2_muls(struct ky_vec2 *vec, float s)
{
    vec->x *= s;
    vec->y *= s;
}

void ky_vec2_divs(struct ky_vec2 *vec, float s)
{
    vec->x /= s;
    vec->y /= s;
}

void ky_vec2_muls_to(struct ky_vec2 *vec, float s, struct ky_vec2 *result)
{
    result->x = vec->x * s;
    result->y = vec->y * s;
}

void ky_vec2_divs_to(struct ky_vec2 *vec, float s, struct ky_vec2 *result)
{
    result->x = vec->x / s;
    result->y = vec->y / s;
}

void ky_vec2_min(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec, struct ky_vec2 *result)
{
    result->x = a_vec->x < b_vec->x ? a_vec->x : b_vec->x;
    result->y = a_vec->y < b_vec->y ? a_vec->y : b_vec->y;
}

void ky_vec2_max(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec, struct ky_vec2 *result)
{
    result->x = a_vec->x > b_vec->x ? a_vec->x : b_vec->x;
    result->y = a_vec->y > b_vec->y ? a_vec->y : b_vec->y;
}
