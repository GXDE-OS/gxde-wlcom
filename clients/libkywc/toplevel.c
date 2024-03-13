// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

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

    free((void *)toplevel->base.uuid);
    free((void *)toplevel->base.title);
    free((void *)toplevel->base.app_id);
    free(toplevel);
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
            ctx->impl->new_toplevel(ctx, &toplevel->base);
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
