// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>
#include <string.h>

#include "kywc-output-v1-client-protocol.h"

#include "libkywc_p.h"

bool _kywc_output_init(kywc_context *ctx, enum kywc_context_capability capability);

static void output_handle_name(void *data, struct kywc_output_v1 *kywc_output_v1, const char *name)
{
    struct ky_output *output = data;
    ky_output_set_name(output, name);
}

static void output_handle_make(void *data, struct kywc_output_v1 *kywc_output_v1, const char *make)
{
    struct ky_output *output = data;
    ky_output_set_make(output, make);
}

static void output_handle_model(void *data, struct kywc_output_v1 *kywc_output_v1,
                                const char *model)
{
    struct ky_output *output = data;
    ky_output_set_model(output, model);
}

static void output_handle_serial_number(void *data, struct kywc_output_v1 *kywc_output_v1,
                                        const char *serial_number)
{
    struct ky_output *output = data;
    ky_output_set_serial_number(output, serial_number);
}

static void output_handle_description(void *data, struct kywc_output_v1 *kywc_output_v1,
                                      const char *description)
{
    struct ky_output *output = data;
    ky_output_set_description(output, description);
}

static void output_handle_physical_size(void *data, struct kywc_output_v1 *kywc_output_v1,
                                        int32_t width, int32_t height)
{
    struct ky_output *output = data;
    ky_output_set_physical_size(output, width, height);
}

static void mode_handle_size(void *data, struct kywc_output_mode_v1 *kywc_output_mode_v1,
                             int32_t width, int32_t height)
{
    struct ky_output_mode *mode = data;
    ky_output_mode_set_size(mode, width, height);
}

static void mode_handle_refresh(void *data, struct kywc_output_mode_v1 *kywc_output_mode_v1,
                                int32_t refresh)
{
    struct ky_output_mode *mode = data;
    ky_output_mode_set_refresh(mode, refresh);
}

static void mode_handle_preferred(void *data, struct kywc_output_mode_v1 *kywc_output_mode_v1)
{
    struct ky_output_mode *mode = data;
    ky_output_mode_set_preferred(mode);
}

static void mode_handle_finished(void *data, struct kywc_output_mode_v1 *kywc_output_mode_v1)
{
    struct ky_output_mode *mode = data;
    ky_output_mode_destroy(mode);
}

static const struct kywc_output_mode_v1_listener mode_listener = {
    .size = mode_handle_size,
    .refresh = mode_handle_refresh,
    .preferred = mode_handle_preferred,
    .finished = mode_handle_finished,
};

static void mode_destroy(struct ky_output_mode *ky_mode)
{
    struct kywc_output_mode_v1 *kywc_output_mode_v1 = ky_mode->data;
    kywc_output_mode_v1_destroy(kywc_output_mode_v1);
}

static void output_handle_mode(void *data, struct kywc_output_v1 *kywc_output_v1,
                               struct kywc_output_mode_v1 *mode)
{
    struct ky_output *output = data;
    struct ky_output_mode *ky_mode = ky_output_mode_create(output);
    if (!ky_mode) {
        return;
    }

    ky_mode->destroy = mode_destroy;
    ky_mode->data = mode;

    kywc_output_mode_v1_add_listener(mode, &mode_listener, ky_mode);
}

static void output_handle_capabilities(void *data, struct kywc_output_v1 *kywc_output_v1,
                                       uint32_t capabilities)
{
    struct ky_output *output = data;
    ky_output_set_capabilities(output, capabilities);
}

static void output_handle_enabled(void *data, struct kywc_output_v1 *kywc_output_v1,
                                  int32_t enabled)
{
    struct ky_output *output = data;
    ky_output_update_enabled(output, enabled);
}

static void output_handle_current_mode(void *data, struct kywc_output_v1 *kywc_output_v1,
                                       struct kywc_output_mode_v1 *kywc_output_mode_v1)
{
    struct ky_output *output = data;
    struct ky_output_mode *mode = kywc_output_mode_v1_get_user_data(kywc_output_mode_v1);
    ky_output_update_current_mode(output, mode);
}

static void output_handle_position(void *data, struct kywc_output_v1 *kywc_output_v1, int32_t x,
                                   int32_t y)
{
    struct ky_output *output = data;
    ky_output_update_position(output, x, y);
}

static void output_handle_transform(void *data, struct kywc_output_v1 *kywc_output_v1,
                                    int32_t transform)
{
    struct ky_output *output = data;
    ky_output_update_transform(output, transform);
}

static void output_handle_scale(void *data, struct kywc_output_v1 *kywc_output_v1, wl_fixed_t scale)
{
    struct ky_output *output = data;
    ky_output_update_scale(output, wl_fixed_to_double(scale));
}

static void output_handle_power(void *data, struct kywc_output_v1 *kywc_output_v1, uint32_t power)
{
    struct ky_output *output = data;
    ky_output_update_power(output, power);
}

static void output_handle_brightness(void *data, struct kywc_output_v1 *kywc_output_v1,
                                     uint32_t brightness)
{
    struct ky_output *output = data;
    ky_output_update_brightness(output, brightness);
}

static void output_handle_color_temp(void *data, struct kywc_output_v1 *kywc_output_v1,
                                     uint32_t color_temp)
{
    struct ky_output *output = data;
    ky_output_update_color_temp(output, color_temp);
}

static void output_handle_finished(void *data, struct kywc_output_v1 *kywc_output_v1)
{
    struct ky_output *output = data;
    ky_output_destroy(output);
}

static const struct kywc_output_v1_listener output_listener = {
    .name = output_handle_name,
    .make = output_handle_make,
    .model = output_handle_model,
    .serial_number = output_handle_serial_number,
    .description = output_handle_description,
    .physical_size = output_handle_physical_size,
    .mode = output_handle_mode,
    .capabilities = output_handle_capabilities,
    .enabled = output_handle_enabled,
    .current_mode = output_handle_current_mode,
    .position = output_handle_position,
    .transform = output_handle_transform,
    .scale = output_handle_scale,
    .power = output_handle_power,
    .brightness = output_handle_brightness,
    .color_temp = output_handle_color_temp,
    .finished = output_handle_finished,
};

static void output_destroy(struct ky_output *output)
{
    struct kywc_output_v1 *kywc_output_v1 = output->data;
    kywc_output_v1_destroy(kywc_output_v1);
    wl_display_flush(output->manager->ctx->display);
}

static void manager_handle_output(void *data, struct kywc_output_manager_v1 *kywc_output_manager_v1,
                                  struct kywc_output_v1 *output, const char *uuid)
{
    struct ky_output_manager *manager = data;
    struct ky_output *ky_output = ky_output_create(manager, uuid);
    if (!ky_output) {
        return;
    }

    ky_output->destroy = output_destroy;
    ky_output->data = output;

    kywc_output_v1_add_listener(output, &output_listener, ky_output);
}

static void manager_handle_primary(void *data,
                                   struct kywc_output_manager_v1 *kywc_output_manager_v1,
                                   struct kywc_output_v1 *output)
{
    struct ky_output_manager *manager = data;
    struct ky_output *ky_output = kywc_output_v1_get_user_data(output);
    ky_output_manager_update_primary(manager, ky_output);
}

static void manager_handle_done(void *data, struct kywc_output_manager_v1 *kywc_output_manager_v1)
{
    struct ky_output_manager *manager = data;
    ky_output_manager_update_states(manager);
}

static void manager_handle_finished(void *data,
                                    struct kywc_output_manager_v1 *kywc_output_manager_v1)
{
    struct ky_output_manager *manager = data;
    kywc_output_manager_v1_destroy(kywc_output_manager_v1);
    wl_display_flush(manager->ctx->display);
}

static const struct kywc_output_manager_v1_listener output_manager_listener = {
    .output = manager_handle_output,
    .primary = manager_handle_primary,
    .done = manager_handle_done,
    .finished = manager_handle_finished,
};

static void manager_destroy(struct ky_output_manager *manager)
{
    struct kywc_output_manager_v1 *output_manager = manager->data;
    kywc_output_manager_v1_stop(output_manager);
    wl_display_flush(manager->ctx->display);
}

static bool output_provider_bind(struct ky_context_provider *provider, struct wl_registry *registry,
                                 uint32_t name, const char *interface, uint32_t version)
{
    if (strcmp(interface, kywc_output_manager_v1_interface.name) == 0) {
        uint32_t version_to_bind = version <= 1 ? version : 1;
        struct ky_output_manager *manager = provider->data;
        struct kywc_output_manager_v1 *output_manager =
            wl_registry_bind(registry, name, &kywc_output_manager_v1_interface, version_to_bind);
        kywc_output_manager_v1_add_listener(output_manager, &output_manager_listener, manager);
        manager->destroy = manager_destroy;
        manager->data = output_manager;
        return true;
    }

    return false;
}

static void output_provider_destroy(struct ky_context_provider *provider)
{
    struct ky_output_manager *manager = provider->data;
    ky_output_manager_destroy(manager);
    free(provider);
}

bool _kywc_output_init(kywc_context *ctx, enum kywc_context_capability capability)
{
    struct ky_context_provider *provider = calloc(1, sizeof(*provider));
    if (!provider) {
        return false;
    }

    wl_list_init(&provider->link);
    provider->capability = capability;
    provider->bind = output_provider_bind;
    provider->destroy = output_provider_destroy;

    struct ky_output_manager *manager = ky_output_manager_create(ctx);
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
