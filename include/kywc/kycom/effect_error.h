// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECT_ERROR_H_
#define _EFFECT_ERROR_H_

enum kywc_effect_error_types {
    EFFECT_ERROR_NO = 0,
    EFFECT_ERROR_SERVER,
    EFFECT_ERROR_MEMORY_OUT,
    EFFECT_ERROR_BUFFER_UNKNOW,
};

enum kywc_effect_error_types kywc_effect_error(void);

char *kywc_effect_error_description(enum kywc_effect_error_types type);

#endif
