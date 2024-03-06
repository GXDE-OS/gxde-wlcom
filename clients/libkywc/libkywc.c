// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "libkywc_p.h"
#include "provider/provider.h"

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version)
{
    kywc_context *ctx = data;

    struct ky_context_provider *provider;
    wl_list_for_each(provider, &ctx->providers, link) {
        if (provider->bind && provider->bind(provider, registry, name, interface, version)) {
            return;
        }
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id)
{
    // kywc_context *ctx = data;
}

static struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static void kywc_context_init_providers(kywc_context *ctx)
{
    wl_list_init(&ctx->providers);

    int num = sizeof(providers) / sizeof(struct ky_provider);
    const struct ky_provider *provider = NULL;

    for (int i = 0; i < num; i++) {
        provider = &providers[i];
        if (ctx->capabilities & provider->capability) {
            provider->init(ctx, provider->capability);
        }
    }
}

kywc_context *kywc_context_create_by_display(struct wl_display *display, uint32_t capabilities,
                                             const struct kywc_context_interface *impl)
{
    kywc_context *ctx = calloc(1, sizeof(kywc_context));
    if (!ctx) {
        return NULL;
    }

    ctx->display = display;
    ctx->capabilities = capabilities;
    ctx->impl = impl;

    // create managers with capabilities by context providers
    kywc_context_init_providers(ctx);

    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);

    wl_display_dispatch(ctx->display);
    wl_display_roundtrip(ctx->display);

    return ctx;
}

kywc_context *kywc_context_create(const char *name, uint32_t capabilities,
                                  const struct kywc_context_interface *impl)
{
    struct wl_display *display = wl_display_connect(name);
    if (!display) {
        fprintf(stderr, "connect to wayland compositor failed\n");
        return NULL;
    }

    return kywc_context_create_by_display(display, capabilities, impl);
}

int kywc_context_get_fd(kywc_context *ctx)
{
    if (!ctx->display) {
        return -1;
    }

    return wl_display_get_fd(ctx->display);
}

void kywc_context_destroy(kywc_context *ctx)
{
    if (!ctx) {
        return;
    }

    struct ky_context_provider *provider, *tmp;
    wl_list_for_each_safe(provider, tmp, &ctx->providers, link) {
        wl_list_remove(&provider->link);
        if (provider->destroy) {
            provider->destroy(provider);
        }
    }

    wl_display_disconnect(ctx->display);
    free(ctx->registry);
    free(ctx);
}

int kywc_context_process(kywc_context *ctx)
{
    if (!ctx) {
        return -1;
    }

    wl_display_prepare_read(ctx->display);
    wl_display_read_events(ctx->display);
    wl_display_dispatch_pending(ctx->display);

    int ret = wl_display_flush(ctx->display);
    if (ret == -1 && errno != EAGAIN) {
        fprintf(stderr, "failed to write wayland fd: %d\n", errno);
        return -1;
    }

    return 0;
}

void kywc_context_dispatch(kywc_context *ctx)
{
    if (!ctx) {
        return;
    }

    while (wl_display_dispatch(ctx->display) != -1) {
        // This space intentionally left blank
    }
}

void kywc_context_set_user_data(kywc_context *ctx, void *data)
{
    if (ctx) {
        ctx->user_data = data;
    }
}

void *kywc_context_get_user_data(kywc_context *ctx)
{
    return ctx ? ctx->user_data : NULL;
}

bool ky_context_add_provider(kywc_context *ctx, struct ky_context_provider *provider, void *manager)
{
    if (provider->capability == KYWC_CONTEXT_CAPABILITY_OUTPUT) {
    } else if (provider->capability == KYWC_CONTEXT_CAPABILITY_TOPLEVEL) {
    } else if (provider->capability == KYWC_CONTEXT_CAPABILITY_WORKSPACE) {
        if (ctx->workspace) {
            return false;
        }
        ctx->workspace = manager;
    }

    wl_list_insert(&ctx->providers, &provider->link);
    return true;
}
