// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _LIBKYWC_HEADER_P_H_
#define _LIBKYWC_HEADER_P_H_

#include "libkywc.h"

struct _kywc_context {
    struct wl_display *display;
    struct wl_registry *registry;

    uint32_t capabilities;
    const struct kywc_context_interface *impl;
    void *user_data;

    struct ky_workspace_manager *workspace;
};

#endif /* _LIBKYWC_HEADER_P_H_ */
