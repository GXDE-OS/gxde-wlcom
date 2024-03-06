// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _LIBKYWC_HEADER_P_H_
#define _LIBKYWC_HEADER_P_H_

#include "libkywc.h"

struct ky_context_provider {
    struct wl_list link;
    enum kywc_context_capability capability;

    bool (*bind)(struct ky_context_provider *provider, struct wl_registry *registry, uint32_t name,
                 const char *interface, uint32_t version);
    void (*destroy)(struct ky_context_provider *provider);
    void *data;
};

struct _kywc_context {
    struct wl_display *display;
    struct wl_registry *registry;

    uint32_t capabilities;
    const struct kywc_context_interface *impl;
    void *user_data;

    struct wl_list providers;

    struct ky_workspace_manager *workspace;
};

bool ky_context_add_provider(kywc_context *ctx, struct ky_context_provider *provider,
                             void *manager);

#endif /* _LIBKYWC_HEADER_P_H_ */
