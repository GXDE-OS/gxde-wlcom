// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include "libkywc_p.h"

static struct ky_workspace *workspace_from_kywc_workspace(kywc_workspace *kywc_workspace)
{
    struct ky_workspace *workspace = wl_container_of(kywc_workspace, workspace, base);
    return workspace;
}

void ky_workspace_destroy(struct ky_workspace *workspace)
{
    if (workspace->impl) {
        workspace->impl->destroy(&workspace->base);
    }

    if (workspace->destroy) {
        workspace->destroy(workspace);
    }

    wl_list_remove(&workspace->link);

    free((void *)workspace->base.uuid);
    free((void *)workspace->base.name);
    free(workspace);
}

void ky_workspace_set_name(struct ky_workspace *workspace, const char *name)
{
    if (workspace->base.name && strcmp(workspace->base.name, name) == 0) {
        return;
    }

    free((void *)workspace->base.name);
    workspace->base.name = strdup(name);
    workspace->pending_mask |= KYWC_WORKSPACE_STATE_NAME;
}

void ky_workspace_set_position(struct ky_workspace *workspace, uint32_t position)
{
    if (workspace->base.position == position) {
        return;
    }

    workspace->base.position = position;
    workspace->pending_mask |= KYWC_WORKSPACE_STATE_POSITION;
}

void ky_workspace_set_activated(struct ky_workspace *workspace, bool activated)
{
    if (workspace->base.activated == activated) {
        return;
    }

    workspace->base.activated = activated;
    workspace->pending_mask |= KYWC_WORKSPACE_STATE_ACTIVATED;
}

struct ky_workspace *ky_workspace_create(struct ky_workspace_manager *manager, const char *uuid)
{
    struct ky_workspace *workspace = calloc(1, sizeof(*workspace));
    if (!workspace) {
        return NULL;
    }

    workspace->manager = manager;
    workspace->newly_added = true;
    workspace->base.uuid = strdup(uuid);
    wl_list_insert(&manager->workspaces, &workspace->link);

    return workspace;
}

void ky_workspace_manager_update(struct ky_workspace_manager *manager)
{
    kywc_context *ctx = manager->ctx;

    struct ky_workspace *workspace;
    wl_list_for_each_reverse(workspace, &manager->workspaces, link) {
        if (workspace->newly_added) {
            if (ctx->impl) {
                ctx->impl->new_workspace(ctx, &workspace->base);
            }
            workspace->newly_added = false;
            workspace->pending_mask = 0;
        } else {
            if (workspace->pending_mask) {
                if (workspace->impl) {
                    workspace->impl->state(&workspace->base, workspace->pending_mask);
                }
                workspace->pending_mask = 0;
            }
        }
    }
}

struct ky_workspace_manager *ky_workspace_manager_create(kywc_context *ctx)
{
    struct ky_workspace_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return NULL;
    }

    manager->ctx = ctx;
    wl_list_init(&manager->workspaces);

    return manager;
}

void ky_workspace_manager_destroy(struct ky_workspace_manager *manager)
{
    if (!manager) {
        return;
    }

    // destroy all workspaces
    struct ky_workspace *workspace, *tmp;
    wl_list_for_each_safe(workspace, tmp, &manager->workspaces, link) {
        ky_workspace_destroy(workspace);
    }

    if (manager->destroy) {
        manager->destroy(manager);
    }

    free(manager);
}

void kywc_workspace_set_interface(kywc_workspace *workspace,
                                  const struct kywc_workspace_interface *impl)
{
    struct ky_workspace *ky_workspace = workspace_from_kywc_workspace(workspace);
    ky_workspace->impl = impl;
}

void kywc_context_for_each_workspace(kywc_context *ctx, kywc_workspace_iterator_func_t iterator,
                                     void *data)
{
    if (!ctx->workspace) {
        return;
    }

    struct ky_workspace *workspace;
    wl_list_for_each_reverse(workspace, &ctx->workspace->workspaces, link) {
        if (iterator(&workspace->base, data)) {
            break;
        }
    }
}

void kywc_workspace_create(kywc_context *ctx, const char *name, uint32_t position)
{
    if (ctx && ctx->workspace && ctx->workspace->create_workspace) {
        ctx->workspace->create_workspace(ctx->workspace, name, position);
    }
}

void kywc_workspace_remove(kywc_workspace *workspace)
{
    struct ky_workspace *ky_workspace = workspace_from_kywc_workspace(workspace);
    if (ky_workspace->remove) {
        ky_workspace->remove(ky_workspace);
    }
}

void kywc_workspace_set_position(kywc_workspace *workspace, uint32_t position)
{
    struct ky_workspace *ky_workspace = workspace_from_kywc_workspace(workspace);
    if (ky_workspace->set_position) {
        ky_workspace->set_position(ky_workspace, position);
    }
}

void kywc_workspace_activate(kywc_workspace *workspace)
{
    struct ky_workspace *ky_workspace = workspace_from_kywc_workspace(workspace);
    if (ky_workspace->activate) {
        ky_workspace->activate(ky_workspace);
    }
}

void kywc_workspace_set_user_data(kywc_workspace *workspace, void *data)
{
    struct ky_workspace *ky_workspace = workspace_from_kywc_workspace(workspace);
    ky_workspace->user_data = data;
}

void *kywc_workspace_get_user_data(kywc_workspace *workspace)
{
    struct ky_workspace *ky_workspace = workspace_from_kywc_workspace(workspace);
    return ky_workspace->user_data;
}
