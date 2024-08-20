// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _LIBKYWC_PROVIDER_H_
#define _LIBKYWC_PROVIDER_H_

#include "libkywc.h"

bool _kywc_workspace_init(kywc_context *ctx, enum kywc_context_capability capability);

bool _kywc_output_init(kywc_context *ctx, enum kywc_context_capability capability);

bool _kywc_toplevel_init(kywc_context *ctx, enum kywc_context_capability capability);

bool _kywc_capture_init(kywc_context *ctx, enum kywc_context_capability capability);

static const struct ky_provider {
    enum kywc_context_capability capability;
    const char *name;
    bool (*init)(kywc_context *ctx, enum kywc_context_capability capability);
} providers[] = {
    { KYWC_CONTEXT_CAPABILITY_WORKSPACE, "kywc_workspace_manager_v1", _kywc_workspace_init },
    { KYWC_CONTEXT_CAPABILITY_OUTPUT, "kywc_output_manager_v1", _kywc_output_init },
    { KYWC_CONTEXT_CAPABILITY_TOPLEVEL, "kywc_toplevel_manager_v1", _kywc_toplevel_init },
    { KYWC_CONTEXT_CAPABILITY_THUMBNAIL, "kywc_capture_manager_v1", _kywc_capture_init },
    { KYWC_CONTEXT_CAPABILITY_THUMBNAIL_EXT, "kywc_capture_manager_v1", _kywc_capture_init },
};

#endif /* _LIBKYWC_PROVIDER_H_ */
