// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include "scene/kycom/effect_error_impl.h"

static enum kywc_effect_error_types this_effect_error = EFFECT_ERROR_NO;

void _kywc_effect_error_set(enum kywc_effect_error_types type)
{
    this_effect_error = type;
}

enum kywc_effect_error_types kywc_effect_error(void)
{
    return this_effect_error;
}

char *kywc_effect_error_description(enum kywc_effect_error_types type)
{
    switch (type) {
    case EFFECT_ERROR_NO:
        break;
    case EFFECT_ERROR_SERVER:
        return "kywc_effect_server is NULL, maybe ky_effect_init failed.";
        break;
    case EFFECT_ERROR_MEMORY_OUT:
        return "Out of memory.";
    default:
        break;
    }

    return "Input params of types error.";
}
