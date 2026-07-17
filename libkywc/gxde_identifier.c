/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This file is part of gxde-daemon.
 *
 * gxde-daemon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gxde-daemon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gxde-daemon.  If not, see <https://www.gnu.org/licenses/>.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include "gxde-identifier-v1-client-protocol.h"
#include "libkywc_p.h"

bool _kywc_gxde_identifier_init(kywc_context *ctx, enum kywc_context_capability capability);

static void identifier_handle_version(void *data,
        struct gxde_identifier_v1 *gxde_identifier_v1, const char *version) {
    struct ky_gxde_identifier *identifier = data;
    if (identifier->version) {
        free(identifier->version);
    }
    identifier->version = strdup(version);
}

static const struct gxde_identifier_v1_listener identifier_listener = {
    .version = identifier_handle_version,
};

static void identifier_destroy(struct ky_gxde_identifier *identifier) {
    struct gxde_identifier_v1 *gxde_identifier_v1 = identifier->data;
    if (gxde_identifier_v1) {
        gxde_identifier_v1_destroy(gxde_identifier_v1);
    }
}

static bool identifier_provider_bind(struct ky_context_provider *provider,
        struct wl_registry *registry, uint32_t name,
        const char *interface, uint32_t version) {
    if (strcmp(interface, gxde_identifier_v1_interface.name) == 0) {
        struct ky_gxde_identifier *identifier = provider->data;
        struct gxde_identifier_v1 *gxde_identifier =
            wl_registry_bind(registry, name, &gxde_identifier_v1_interface,
                version);
        gxde_identifier_v1_add_listener(gxde_identifier, &identifier_listener, identifier);
        identifier->destroy = identifier_destroy;
        identifier->data = gxde_identifier;
        return true;
    }

    return false;
}

static void identifier_provider_destroy(struct ky_context_provider *provider) {
    struct ky_gxde_identifier *identifier = provider->data;
    if (identifier->destroy) {
        identifier->destroy(identifier);
    }

    if (identifier->version) {
        free(identifier->version);
    }
    free(identifier);
    free(provider);
}

bool _kywc_gxde_identifier_init(kywc_context *ctx, enum kywc_context_capability capability) {
    struct ky_context_provider *provider = calloc(1, sizeof(*provider));
    if (!provider) {
        return false;
    }

    wl_list_init(&provider->link);
    provider->capability = capability;
    provider->bind = identifier_provider_bind;
    provider->destroy = identifier_provider_destroy;

    struct ky_gxde_identifier *identifier = calloc(1, sizeof(*identifier));
    if (!identifier) {
        free(provider);
        return false;
    }

    provider->data = identifier;

    if (!ky_context_add_provider(ctx, provider, identifier)) {
        free(identifier);
        free(provider);
        return false;
    }

    return true;
}
