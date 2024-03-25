// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>
#include <string.h>

#include "kywc-toplevel-v1-client-protocol.h"

#include "libkywc_p.h"

bool _kywc_toplevel_init(kywc_context *ctx, enum kywc_context_capability capability);

static void toplevel_handle_closed(void *data, struct kywc_toplevel_v1 *kywc_toplevel_v1)
{
    struct ky_toplevel *toplevel = data;
    ky_toplevel_destroy(toplevel);
}

static void toplevel_handle_done(void *data, struct kywc_toplevel_v1 *kywc_toplevel_v1)
{
    struct ky_toplevel *toplevel = data;
    ky_toplevel_update_states(toplevel);
}

static void toplevel_handle_title(void *data, struct kywc_toplevel_v1 *kywc_toplevel_v1,
                                  const char *title)
{
    struct ky_toplevel *toplevel = data;
    ky_toplevel_update_title(toplevel, title);
}

static void toplevel_handle_app_id(void *data, struct kywc_toplevel_v1 *kywc_toplevel_v1,
                                   const char *app_id)
{
    struct ky_toplevel *toplevel = data;
    ky_toplevel_update_app_id(toplevel, app_id);
}

static void toplevel_handle_primary_output(void *data, struct kywc_toplevel_v1 *kywc_toplevel_v1,
                                           const char *output)
{
    struct ky_toplevel *toplevel = data;
    ky_toplevel_update_primary_output(toplevel, output);
}

static void toplevel_handle_workspace_enter(void *data, struct kywc_toplevel_v1 *kywc_toplevel_v1,
                                            const char *workspace)
{
    struct ky_toplevel *toplevel = data;
    ky_toplevel_enter_workspace(toplevel, workspace);
}

static void toplevel_handle_workspace_leave(void *data, struct kywc_toplevel_v1 *kywc_toplevel_v1,
                                            const char *workspace)
{
    struct ky_toplevel *toplevel = data;
    ky_toplevel_leave_workspace(toplevel, workspace);
}

static void toplevel_handle_state(void *data, struct kywc_toplevel_v1 *kywc_toplevel_v1,
                                  uint32_t state)
{
    struct ky_toplevel *toplevel = data;
    ky_toplevel_update_maximized(toplevel, state & KYWC_TOPLEVEL_V1_STATE_MAXIMIZED);
    ky_toplevel_update_minimized(toplevel, state & KYWC_TOPLEVEL_V1_STATE_MINIMIZED);
    ky_toplevel_update_activated(toplevel, state & KYWC_TOPLEVEL_V1_STATE_ACTIVATED);
    ky_toplevel_update_fullscreen(toplevel, state & KYWC_TOPLEVEL_V1_STATE_FULLSCREEN);
}

static void toplevel_handle_parent(void *data, struct kywc_toplevel_v1 *kywc_toplevel_v1,
                                   struct kywc_toplevel_v1 *parent)
{
    struct ky_toplevel *toplevel = data;
    struct ky_toplevel *parent_toplevel = parent ? kywc_toplevel_v1_get_user_data(parent) : NULL;
    ky_toplevel_update_parent(toplevel, parent_toplevel);
}

static void toplevel_handle_icon(void *data, struct kywc_toplevel_v1 *kywc_toplevel_v1,
                                 const char *name)
{
    struct ky_toplevel *toplevel = data;
    ky_toplevel_update_icon(toplevel, name);
}

static const struct kywc_toplevel_v1_listener toplevel_listener = {
    .closed = toplevel_handle_closed,
    .done = toplevel_handle_done,
    .title = toplevel_handle_title,
    .app_id = toplevel_handle_app_id,
    .primary_output = toplevel_handle_primary_output,
    .workspace_enter = toplevel_handle_workspace_enter,
    .workspace_leave = toplevel_handle_workspace_leave,
    .state = toplevel_handle_state,
    .parent = toplevel_handle_parent,
    .icon = toplevel_handle_icon,
};

static void toplevel_destroy(struct ky_toplevel *toplevel)
{
    struct kywc_toplevel_v1 *kywc_toplevel_v1 = toplevel->data;
    kywc_toplevel_v1_destroy(kywc_toplevel_v1);
}

static void manager_handle_toplevel(void *data,
                                    struct kywc_toplevel_manager_v1 *kywc_toplevel_manager_v1,
                                    struct kywc_toplevel_v1 *kywc_toplevel_v1, const char *uuid)
{
    struct ky_toplevel_manager *manager = data;
    struct ky_toplevel *toplevel = ky_toplevel_create(manager, uuid);
    if (!toplevel) {
        return;
    }

    toplevel->destroy = toplevel_destroy;

    toplevel->data = kywc_toplevel_v1;
    kywc_toplevel_v1_add_listener(kywc_toplevel_v1, &toplevel_listener, toplevel);
}

static void manager_handle_finished(void *data,
                                    struct kywc_toplevel_manager_v1 *kywc_toplevel_manager_v1)
{
    kywc_toplevel_manager_v1_destroy(kywc_toplevel_manager_v1);
}

static const struct kywc_toplevel_manager_v1_listener toplevel_manager_listener = {
    .toplevel = manager_handle_toplevel,
    .finished = manager_handle_finished,
};

static void manager_destroy(struct ky_toplevel_manager *manager)
{
    struct kywc_toplevel_manager_v1 *toplevel_manager = manager->data;
    kywc_toplevel_manager_v1_stop(toplevel_manager);
}

static bool toplevel_provider_bind(struct ky_context_provider *provider,
                                   struct wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version)
{
    if (strcmp(interface, kywc_toplevel_manager_v1_interface.name) == 0) {
        uint32_t version_to_bind = version <= 1 ? version : 1;
        struct ky_toplevel_manager *manager = provider->data;
        struct kywc_toplevel_manager_v1 *toplevel_manager =
            wl_registry_bind(registry, name, &kywc_toplevel_manager_v1_interface, version_to_bind);
        kywc_toplevel_manager_v1_add_listener(toplevel_manager, &toplevel_manager_listener,
                                              manager);
        manager->destroy = manager_destroy;
        manager->data = toplevel_manager;
        return true;
    }

    return false;
}

static void toplevel_provider_destroy(struct ky_context_provider *provider)
{
    struct ky_toplevel_manager *manager = provider->data;
    ky_toplevel_manager_destroy(manager);
    free(provider);
}

bool _kywc_toplevel_init(kywc_context *ctx, enum kywc_context_capability capability)
{
    struct ky_context_provider *provider = calloc(1, sizeof(*provider));
    if (!provider) {
        return false;
    }

    wl_list_init(&provider->link);
    provider->capability = capability;
    provider->bind = toplevel_provider_bind;
    provider->destroy = toplevel_provider_destroy;

    struct ky_toplevel_manager *manager = ky_toplevel_manager_create(ctx);
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
