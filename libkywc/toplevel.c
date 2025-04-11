// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include "libkywc_p.h"

static struct ky_toplevel *toplevel_from_kywc_toplevel(kywc_toplevel *kywc_toplevel)
{
    struct ky_toplevel *toplevel = wl_container_of(kywc_toplevel, toplevel, base);
    return toplevel;
}

void ky_toplevel_destroy(struct ky_toplevel *toplevel)
{
    if (toplevel->impl && toplevel->impl->destroy) {
        toplevel->impl->destroy(&toplevel->base);
    }

    if (toplevel->destroy) {
        toplevel->destroy(toplevel);
    }

    wl_list_remove(&toplevel->link);

    for (int i = 0; i < MAX_WORKSPACES; i++) {
        free((void *)toplevel->base.workspaces[i]);
    }

    free((void *)toplevel->base.uuid);
    free((void *)toplevel->base.title);
    free((void *)toplevel->base.app_id);
    free((void *)toplevel->base.icon);
    free((void *)toplevel->base.primary_output);
    free(toplevel);
}

void ky_toplevel_set_capabilities(struct ky_toplevel *toplevel, uint32_t capabilities)
{
    toplevel->base.capabilities = capabilities;
}

void ky_toplevel_update_title(struct ky_toplevel *toplevel, const char *title)
{
    if (toplevel->base.title && strcmp(toplevel->base.title, title) == 0) {
        return;
    }

    free((void *)toplevel->base.title);
    toplevel->base.title = strdup(title);
    toplevel->pending_mask |= KYWC_TOPLEVEL_STATE_TITLE;
}

void ky_toplevel_update_app_id(struct ky_toplevel *toplevel, const char *app_id)
{
    if (toplevel->base.app_id && strcmp(toplevel->base.app_id, app_id) == 0) {
        return;
    }

    free((void *)toplevel->base.app_id);
    toplevel->base.app_id = strdup(app_id);
    toplevel->pending_mask |= KYWC_TOPLEVEL_STATE_APP_ID;
}

void ky_toplevel_update_primary_output(struct ky_toplevel *toplevel, const char *output_id)
{
    if (toplevel->base.primary_output && strcmp(toplevel->base.primary_output, output_id) == 0) {
        return;
    }

    free((void *)toplevel->base.primary_output);
    toplevel->base.primary_output = strdup(output_id);
    toplevel->pending_mask |= KYWC_TOPLEVEL_STATE_PRIMARY_OUTPUT;
}

void ky_toplevel_update_maximized(struct ky_toplevel *toplevel, bool maximized)
{
    if (toplevel->base.maximized == maximized) {
        return;
    }

    toplevel->base.maximized = maximized;
    toplevel->pending_mask |= KYWC_TOPLEVEL_STATE_MAXIMIZED;
}

void ky_toplevel_update_minimized(struct ky_toplevel *toplevel, bool minimized)
{
    if (toplevel->base.minimized == minimized) {
        return;
    }

    toplevel->base.minimized = minimized;
    toplevel->pending_mask |= KYWC_TOPLEVEL_STATE_MINIMIZED;
}

void ky_toplevel_update_activated(struct ky_toplevel *toplevel, bool activated)
{
    if (toplevel->base.activated == activated) {
        return;
    }

    toplevel->base.activated = activated;
    toplevel->pending_mask |= KYWC_TOPLEVEL_STATE_ACTIVATED;
}

void ky_toplevel_update_fullscreen(struct ky_toplevel *toplevel, bool fullscreen)
{
    if (toplevel->base.fullscreen == fullscreen) {
        return;
    }

    toplevel->base.fullscreen = fullscreen;
    toplevel->pending_mask |= KYWC_TOPLEVEL_STATE_FULLSCREEN;
}

void ky_toplevel_update_parent(struct ky_toplevel *toplevel, struct ky_toplevel *parent)
{
    kywc_toplevel *parent_toplevel = parent ? &parent->base : NULL;
    if (toplevel->base.parent == parent_toplevel) {
        return;
    }

    toplevel->base.parent = parent_toplevel;
    toplevel->pending_mask |= KYWC_TOPLEVEL_STATE_PARENT;
}

void ky_toplevel_update_icon(struct ky_toplevel *toplevel, const char *icon)
{
    if (toplevel->base.icon && strcmp(toplevel->base.icon, icon) == 0) {
        return;
    }

    free((void *)toplevel->base.icon);
    toplevel->base.icon = strdup(icon);
    toplevel->pending_mask |= KYWC_TOPLEVEL_STATE_ICON;
}

void ky_toplevel_update_geometry(struct ky_toplevel *toplevel, int32_t x, int32_t y, uint32_t width,
                                 uint32_t height)
{
    if (toplevel->base.x != x || toplevel->base.y != y) {
        toplevel->base.x = x;
        toplevel->base.y = y;
        toplevel->pending_mask |= KYWC_TOPLEVEL_STATE_POSITION;
    }

    if (toplevel->base.width != width || toplevel->base.height != height) {
        toplevel->base.width = width;
        toplevel->base.height = height;
        toplevel->pending_mask |= KYWC_TOPLEVEL_STATE_SIZE;
    }
}

void ky_toplevel_enter_workspace(struct ky_toplevel *toplevel, const char *workspace)
{
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        if (toplevel->base.workspaces[i] == NULL) {
            toplevel->base.workspaces[i] = strdup(workspace);
            break;
        }
    }

    toplevel->pending_mask |= KYWC_TOPLEVEL_STATE_WORKSPACE;
}

void ky_toplevel_leave_workspace(struct ky_toplevel *toplevel, const char *workspace)
{
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        if (toplevel->base.workspaces[i] && strcmp(toplevel->base.workspaces[i], workspace) == 0) {
            free((void *)toplevel->base.workspaces[i]);
            toplevel->base.workspaces[i] = NULL;
            break;
        }
    }

    toplevel->pending_mask |= KYWC_TOPLEVEL_STATE_WORKSPACE;
}

struct ky_toplevel *ky_toplevel_create(struct ky_toplevel_manager *manager, const char *uuid)
{
    struct ky_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    if (!toplevel) {
        return NULL;
    }

    toplevel->manager = manager;
    toplevel->newly_added = true;
    toplevel->base.uuid = strdup(uuid);
    wl_list_insert(&manager->toplevels, &toplevel->link);

    return toplevel;
}

void ky_toplevel_update_states(struct ky_toplevel *toplevel)
{
    kywc_context *ctx = toplevel->manager->ctx;

    if (toplevel->newly_added) {
        if (ctx->impl && ctx->impl->new_toplevel) {
            ctx->impl->new_toplevel(ctx, &toplevel->base, ctx->user_data);
        }
        toplevel->newly_added = false;
        toplevel->pending_mask = 0;
    } else {
        if (toplevel->pending_mask) {
            if (toplevel->impl && toplevel->impl->state) {
                toplevel->impl->state(&toplevel->base, toplevel->pending_mask);
            }
            toplevel->pending_mask = 0;
        }
    }
}

struct ky_toplevel_manager *ky_toplevel_manager_create(kywc_context *ctx)
{
    struct ky_toplevel_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return NULL;
    }

    manager->ctx = ctx;
    wl_list_init(&manager->toplevels);

    return manager;
}

void ky_toplevel_manager_destroy(struct ky_toplevel_manager *manager)
{
    if (!manager) {
        return;
    }

    // destroy all toplevels
    struct ky_toplevel *toplevel, *tmp;
    wl_list_for_each_safe(toplevel, tmp, &manager->toplevels, link) {
        ky_toplevel_destroy(toplevel);
    }

    if (manager->destroy) {
        manager->destroy(manager);
    }

    free(manager);
}

void kywc_toplevel_set_interface(kywc_toplevel *toplevel,
                                 const struct kywc_toplevel_interface *impl)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    ky_toplevel->impl = impl;
}

void kywc_context_for_each_toplevel(kywc_context *ctx, kywc_toplevel_iterator_func_t iterator,
                                    void *data)
{
    if (!ctx->toplevel) {
        return;
    }

    struct ky_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &ctx->toplevel->toplevels, link) {
        if (iterator(&toplevel->base, data)) {
            break;
        }
    }
}

kywc_context *kywc_toplevel_get_context(kywc_toplevel *toplevel)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    return ky_toplevel->manager->ctx;
}

kywc_toplevel *kywc_context_find_toplevel(kywc_context *ctx, const char *uuid)
{
    if (!ctx->toplevel || !uuid) {
        return NULL;
    }

    struct ky_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &ctx->toplevel->toplevels, link) {
        if (strcmp(toplevel->base.uuid, uuid) == 0) {
            return &toplevel->base;
        }
    }

    return NULL;
}

bool kywc_toplevel_has_children(kywc_toplevel *toplevel)
{
    kywc_context *ctx = kywc_toplevel_get_context(toplevel);

    struct ky_toplevel *ky_toplevel;
    wl_list_for_each_reverse(ky_toplevel, &ctx->toplevel->toplevels, link) {
        if (ky_toplevel->base.parent == toplevel) {
            return true;
        }
    }

    return false;
}

void kywc_toplevel_set_maximized(kywc_toplevel *toplevel, const char *output)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    if (ky_toplevel->set_maximized) {
        ky_toplevel->set_maximized(ky_toplevel, output);
    }
}

void kywc_toplevel_unset_maximized(kywc_toplevel *toplevel)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    if (ky_toplevel->unset_maximized) {
        ky_toplevel->unset_maximized(ky_toplevel);
    }
}

void kywc_toplevel_set_minimized(kywc_toplevel *toplevel)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    if (ky_toplevel->set_minimized) {
        ky_toplevel->set_minimized(ky_toplevel);
    }
}

void kywc_toplevel_unset_minimized(kywc_toplevel *toplevel)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    if (ky_toplevel->unset_minimized) {
        ky_toplevel->unset_minimized(ky_toplevel);
    }
}

void kywc_toplevel_set_fullscreen(kywc_toplevel *toplevel, const char *output)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    if (ky_toplevel->set_fullscreen) {
        ky_toplevel->set_fullscreen(ky_toplevel, output);
    }
}

void kywc_toplevel_unset_fullscreen(kywc_toplevel *toplevel)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    if (ky_toplevel->unset_fullscreen) {
        ky_toplevel->unset_fullscreen(ky_toplevel);
    }
}

void kywc_toplevel_activate(kywc_toplevel *toplevel)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    if (ky_toplevel->activate) {
        ky_toplevel->activate(ky_toplevel);
    }
}

void kywc_toplevel_close(kywc_toplevel *toplevel)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    if (ky_toplevel->close) {
        ky_toplevel->close(ky_toplevel);
    }
}

void kywc_toplevel_enter_workspace(kywc_toplevel *toplevel, const char *workspace)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    if (ky_toplevel->enter_workspace) {
        ky_toplevel->enter_workspace(ky_toplevel, workspace);
    }
}

void kywc_toplevel_leave_workspace(kywc_toplevel *toplevel, const char *workspace)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    if (ky_toplevel->leave_workspace) {
        ky_toplevel->leave_workspace(ky_toplevel, workspace);
    }
}

void kywc_toplevel_move_to_workspace(kywc_toplevel *toplevel, const char *workspace)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    if (ky_toplevel->move_to_workspace) {
        ky_toplevel->move_to_workspace(ky_toplevel, workspace);
    }
}

void kywc_toplevel_move_to_output(kywc_toplevel *toplevel, const char *output)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    if (ky_toplevel->move_to_output) {
        ky_toplevel->move_to_output(ky_toplevel, output);
    }
}

void kywc_toplevel_set_user_data(kywc_toplevel *toplevel, void *data)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    ky_toplevel->user_data = data;
}

void *kywc_toplevel_get_user_data(kywc_toplevel *toplevel)
{
    struct ky_toplevel *ky_toplevel = toplevel_from_kywc_toplevel(toplevel);
    return ky_toplevel->user_data;
}
