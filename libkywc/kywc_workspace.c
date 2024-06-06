// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>
#include <string.h>

#include "kywc-workspace-v1-client-protocol.h"

#include "libkywc_p.h"

bool _kywc_workspace_init(kywc_context *ctx, enum kywc_context_capability capability);

static void workspace_handle_name(void *data, struct kywc_workspace_v1 *kywc_workspace_v1,
                                  const char *name)
{
    struct ky_workspace *workspace = data;
    ky_workspace_update_name(workspace, name);
}

static void workspace_handle_position(void *data, struct kywc_workspace_v1 *kywc_workspace_v1,
                                      uint32_t position)
{
    struct ky_workspace *workspace = data;
    ky_workspace_update_position(workspace, position);
}

static void workspace_handle_activated(void *data, struct kywc_workspace_v1 *kywc_workspace_v1)
{
    struct ky_workspace *workspace = data;
    ky_workspace_update_activated(workspace, true);
}

static void workspace_handle_deactivated(void *data, struct kywc_workspace_v1 *kywc_workspace_v1)
{
    struct ky_workspace *workspace = data;
    ky_workspace_update_activated(workspace, false);
}

static void workspace_handle_removed(void *data, struct kywc_workspace_v1 *kywc_workspace_v1)
{
    struct ky_workspace *workspace = data;
    ky_workspace_destroy(workspace);
}

static const struct kywc_workspace_v1_listener workspace_listener = {
    .name = workspace_handle_name,
    .position = workspace_handle_position,
    .activated = workspace_handle_activated,
    .deactivated = workspace_handle_deactivated,
    .removed = workspace_handle_removed,
};

static void workspace_destroy(struct ky_workspace *workspace)
{
    struct kywc_workspace_v1 *kywc_workspace_v1 = workspace->data;
    kywc_workspace_v1_destroy(kywc_workspace_v1);
    wl_display_flush(workspace->manager->ctx->display);
}

static void workspace_set_position(struct ky_workspace *workspace, uint32_t postion)
{
    struct kywc_workspace_v1 *kywc_workspace_v1 = workspace->data;
    kywc_workspace_v1_set_position(kywc_workspace_v1, postion);
    wl_display_flush(workspace->manager->ctx->display);
}

static void workspace_activate(struct ky_workspace *workspace)
{
    struct kywc_workspace_v1 *kywc_workspace_v1 = workspace->data;
    kywc_workspace_v1_activate(kywc_workspace_v1);
    wl_display_flush(workspace->manager->ctx->display);
}

static void workspace_remove(struct ky_workspace *workspace)
{
    struct kywc_workspace_v1 *kywc_workspace_v1 = workspace->data;
    kywc_workspace_v1_remove(kywc_workspace_v1);
    wl_display_flush(workspace->manager->ctx->display);
}

static void manager_handle_workspace(void *data,
                                     struct kywc_workspace_manager_v1 *kywc_workspace_manager_v1,
                                     struct kywc_workspace_v1 *kywc_workspace_v1, const char *uuid)
{
    struct ky_workspace_manager *manager = data;
    struct ky_workspace *workspace = ky_workspace_create(manager, uuid);
    if (!workspace) {
        return;
    }

    workspace->set_position = workspace_set_position;
    workspace->activate = workspace_activate;
    workspace->remove = workspace_remove;
    workspace->destroy = workspace_destroy;

    workspace->data = kywc_workspace_v1;
    kywc_workspace_v1_add_listener(kywc_workspace_v1, &workspace_listener, workspace);
}

static void manager_handle_done(void *data,
                                struct kywc_workspace_manager_v1 *kywc_workspace_manager_v1)
{
    struct ky_workspace_manager *manager = data;
    ky_workspace_manager_update_states(manager);
}

static void manager_handle_finished(void *data,
                                    struct kywc_workspace_manager_v1 *kywc_workspace_manager_v1)
{
    kywc_workspace_manager_v1_destroy(kywc_workspace_manager_v1);
}

static const struct kywc_workspace_manager_v1_listener workspace_manager_listener = {
    .workspace = manager_handle_workspace,
    .done = manager_handle_done,
    .finished = manager_handle_finished,
};

static void manager_create_workspace(struct ky_workspace_manager *manager, const char *name,
                                     uint32_t position)
{
    struct kywc_workspace_manager_v1 *workspace_manager = manager->data;
    kywc_workspace_manager_v1_create_workspace(workspace_manager, name, position);
    wl_display_flush(manager->ctx->display);
}

static void manager_destroy(struct ky_workspace_manager *manager)
{
    struct kywc_workspace_manager_v1 *workspace_manager = manager->data;
    kywc_workspace_manager_v1_stop(workspace_manager);
    wl_display_flush(manager->ctx->display);
}

static bool workspace_provider_bind(struct ky_context_provider *provider,
                                    struct wl_registry *registry, uint32_t name,
                                    const char *interface, uint32_t version)
{
    if (strcmp(interface, kywc_workspace_manager_v1_interface.name) == 0) {
        uint32_t version_to_bind = version <= 1 ? version : 1;
        struct ky_workspace_manager *manager = provider->data;
        struct kywc_workspace_manager_v1 *workspace_manager =
            wl_registry_bind(registry, name, &kywc_workspace_manager_v1_interface, version_to_bind);
        kywc_workspace_manager_v1_add_listener(workspace_manager, &workspace_manager_listener,
                                               manager);
        manager->create_workspace = manager_create_workspace;
        manager->destroy = manager_destroy;
        manager->data = workspace_manager;
        return true;
    }

    return false;
}

static void workspace_provider_destroy(struct ky_context_provider *provider)
{
    struct ky_workspace_manager *manager = provider->data;
    ky_workspace_manager_destroy(manager);
    free(provider);
}

bool _kywc_workspace_init(kywc_context *ctx, enum kywc_context_capability capability)
{
    struct ky_context_provider *provider = calloc(1, sizeof(*provider));
    if (!provider) {
        return false;
    }

    wl_list_init(&provider->link);
    provider->capability = capability;
    provider->bind = workspace_provider_bind;
    provider->destroy = workspace_provider_destroy;

    struct ky_workspace_manager *manager = ky_workspace_manager_create(ctx);
    if (!manager) {
        free(provider);
        return false;
    }

    provider->data = manager;

    if (!ky_context_add_provider(ctx, provider, manager)) {
        free(manager);
        free(provider);
        return false;
    }

    return true;
}
