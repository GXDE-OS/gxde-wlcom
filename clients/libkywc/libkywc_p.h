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

/**
 * workspace
 */
struct ky_workspace_manager {
    kywc_context *ctx;
    struct wl_list workspaces;

    void (*create_workspace)(struct ky_workspace_manager *manager, const char *name,
                             uint32_t position);
    void (*destroy)(struct ky_workspace_manager *manager);
    void *data;
};

struct ky_workspace {
    kywc_workspace base;

    struct ky_workspace_manager *manager;
    struct wl_list link;

    const struct kywc_workspace_interface *impl;
    void *user_data;

    void (*set_position)(struct ky_workspace *workspace, uint32_t postion);
    void (*activate)(struct ky_workspace *workspace);
    void (*remove)(struct ky_workspace *workspace);
    void (*destroy)(struct ky_workspace *workspace);
    void *data;

    uint32_t pending_mask;
    bool newly_added;
};

struct ky_workspace_manager *ky_workspace_manager_create(kywc_context *ctx);

void ky_workspace_manager_destroy(struct ky_workspace_manager *manager);

void ky_workspace_manager_update(struct ky_workspace_manager *manager);

struct ky_workspace *ky_workspace_create(struct ky_workspace_manager *manager, const char *uuid);

void ky_workspace_destroy(struct ky_workspace *workspace);

void ky_workspace_set_name(struct ky_workspace *workspace, const char *name);

void ky_workspace_set_position(struct ky_workspace *workspace, uint32_t position);

void ky_workspace_set_activated(struct ky_workspace *workspace, bool activated);

#endif /* _LIBKYWC_HEADER_P_H_ */
