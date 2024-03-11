// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _LIBKYWC_PROVIDER_H_
#define _LIBKYWC_PROVIDER_H_

#include "libkywc.h"

bool _kywc_workspace_init(kywc_context *ctx, enum kywc_context_capability capability);

static const struct ky_provider {
    enum kywc_context_capability capability;
    const char *name;
    bool (*init)(kywc_context *ctx, enum kywc_context_capability capability);
} providers[] = {
    { KYWC_CONTEXT_CAPABILITY_WORKSPACE, "kywc_workspace_manager_v1", _kywc_workspace_init },
};

#endif /* _LIBKYWC_PROVIDER_H_ */
