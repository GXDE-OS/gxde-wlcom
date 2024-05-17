// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _UTIL_VEC2_H_
#define _UTIL_VEC2_H_

struct ky_vec2 {
    float x;
    float y;
};

// length square
float ky_vec2_length2(struct ky_vec2 *vec);

float ky_vec2_length(struct ky_vec2 *vec);

// distance square
float ky_vec2_distance2(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec);

float ky_vec2_distance(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec);

void ky_vec2_normalize(struct ky_vec2 *vec);

void ky_vec2_normalize_to(struct ky_vec2 *vec, struct ky_vec2 *result);

void ky_vec2_perpendicular(struct ky_vec2 *vec, struct ky_vec2 *result);

void ky_vec2_add(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec, struct ky_vec2 *result);

void ky_vec2_sub(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec, struct ky_vec2 *result);

void ky_vec2_mul(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec, struct ky_vec2 *result);

void ky_vec2_div(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec, struct ky_vec2 *result);

void ky_vec2_muls(struct ky_vec2 *vec, float s);

void ky_vec2_divs(struct ky_vec2 *vec, float s);

void ky_vec2_muls_to(struct ky_vec2 *vec, float s, struct ky_vec2 *result);

void ky_vec2_divs_to(struct ky_vec2 *vec, float s, struct ky_vec2 *result);

void ky_vec2_min(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec, struct ky_vec2 *result);

void ky_vec2_max(struct ky_vec2 *a_vec, struct ky_vec2 *b_vec, struct ky_vec2 *result);

#endif /* _UTIL_VEC2_H_ */
