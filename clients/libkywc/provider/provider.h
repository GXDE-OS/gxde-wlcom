// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _LIBKYWC_PROVIDER_H_
#define _LIBKYWC_PROVIDER_H_

#include "libkywc_p.h"

static const struct ky_provider {
    enum kywc_context_capability capability;
    const char *name;
    bool (*init)(kywc_context *ctx, enum kywc_context_capability capability);
} providers[] = {};

#endif /* _LIBKYWC_PROVIDER_H_ */
