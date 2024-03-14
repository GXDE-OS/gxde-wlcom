// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "libkywc_p.h"

static struct ky_output *output_from_kywc_output(kywc_output *kywc_output)
{
    struct ky_output *output = wl_container_of(kywc_output, output, base);
    return output;
}

void ky_output_destroy(struct ky_output *output)
{
    struct ky_output_mode *mode, *tmp;
    wl_list_for_each_safe(mode, tmp, &output->base.modes, base.link) {
        ky_output_mode_destroy(mode);
    }

    if (output->impl && output->impl->destroy) {
        output->impl->destroy(&output->base);
    }

    if (output->destroy) {
        output->destroy(output);
    }

    wl_list_remove(&output->link);

    free((void *)output->base.uuid);
    free((void *)output->base.name);
    free((void *)output->base.make);
    free((void *)output->base.model);
    free((void *)output->base.serial);
    free((void *)output->base.description);
    free(output);
}

void ky_output_set_name(struct ky_output *output, const char *name)
{
    assert(output->base.name == NULL);
    output->base.name = strdup(name);
}

void ky_output_set_make(struct ky_output *output, const char *make)
{
    assert(output->base.make == NULL);
    output->base.make = strdup(make);
}

void ky_output_set_model(struct ky_output *output, const char *model)
{
    assert(output->base.model == NULL);
    output->base.model = strdup(model);
}

void ky_output_set_serial_number(struct ky_output *output, const char *serial_number)
{
    assert(output->base.serial == NULL);
    output->base.serial = strdup(serial_number);
}

void ky_output_set_description(struct ky_output *output, const char *description)
{
    assert(output->base.description == NULL);
    output->base.description = strdup(description);
}

void ky_output_set_physical_size(struct ky_output *output, int width, int height)
{
    output->base.physical_width = width;
    output->base.physical_height = height;
}

void ky_output_set_capabilities(struct ky_output *output, uint32_t capabilities)
{
    output->base.capabilities = capabilities;
}

struct ky_output_mode *ky_output_mode_create(struct ky_output *output)
{
    struct ky_output_mode *mode = calloc(1, sizeof(*mode));
    if (!mode) {
        return NULL;
    }

    mode->output = output;
    wl_list_insert(&output->base.modes, &mode->base.link);

    return mode;
}

void ky_output_mode_set_size(struct ky_output_mode *mode, int width, int height)
{
    mode->base.width = width;
    mode->base.height = height;
}

void ky_output_mode_set_refresh(struct ky_output_mode *mode, int refresh)
{
    mode->base.refresh = refresh;
}

void ky_output_mode_set_preferred(struct ky_output_mode *mode)
{
    mode->base.preferred = true;
}

void ky_output_mode_destroy(struct ky_output_mode *mode)
{
    if (mode->destroy) {
        mode->destroy(mode);
    }

    wl_list_remove(&mode->base.link);
    free(mode);
}

void ky_output_update_enabled(struct ky_output *output, bool enabled)
{
    if (output->base.enabled == enabled) {
        return;
    }

    output->base.enabled = enabled;
    if (!enabled) {
        output->base.mode = NULL;
    }
    output->pending_mask |= KYWC_OUTPUT_STATE_ENABLED;
}

void ky_output_update_current_mode(struct ky_output *output, struct ky_output_mode *mode)
{
    if (output->base.mode == &mode->base) {
        return;
    }

    output->base.mode = &mode->base;
    output->pending_mask |= KYWC_OUTPUT_STATE_MODE;
}

void ky_output_update_position(struct ky_output *output, int x, int y)
{
    if (output->base.x == x && output->base.y == y) {
        return;
    }

    output->base.x = x;
    output->base.y = y;
    output->pending_mask |= KYWC_OUTPUT_STATE_POSITION;
}

void ky_output_update_transform(struct ky_output *output, int transform)
{
    if (output->base.transform == transform) {
        return;
    }

    output->base.transform = transform;
    output->pending_mask |= KYWC_OUTPUT_STATE_TRANSFROM;
}

void ky_output_update_scale(struct ky_output *output, float scale)
{
    if (output->base.scale == scale) {
        return;
    }

    output->base.scale = scale;
    output->pending_mask |= KYWC_OUTPUT_STATE_SCALE;
}

void ky_output_update_power(struct ky_output *output, bool power)
{
    if (output->base.power == power) {
        return;
    }

    output->base.power = power;
    output->pending_mask |= KYWC_OUTPUT_STATE_POWER;
}

void ky_output_update_brightness(struct ky_output *output, uint32_t brightness)
{
    if (output->base.brightness == brightness) {
        return;
    }

    output->base.brightness = brightness;
    output->pending_mask |= KYWC_OUTPUT_STATE_BRIGHTNESS;
}

void ky_output_update_color_temp(struct ky_output *output, uint32_t color_temp)
{
    if (output->base.color_temp == color_temp) {
        return;
    }

    output->base.color_temp = color_temp;
    output->pending_mask |= KYWC_OUTPUT_STATE_COLOR_TEMP;
}

struct ky_output *ky_output_create(struct ky_output_manager *manager, const char *uuid)
{
    struct ky_output *output = calloc(1, sizeof(*output));
    if (!output) {
        return NULL;
    }

    output->manager = manager;
    output->base.scale = 1.0;
    output->newly_added = true;
    output->base.uuid = strdup(uuid);
    wl_list_init(&output->base.modes);
    wl_list_insert(&manager->outputs, &output->link);

    return output;
}

void ky_output_manager_update_states(struct ky_output_manager *manager)
{
    kywc_context *ctx = manager->ctx;

    struct ky_output *output;
    wl_list_for_each_reverse(output, &manager->outputs, link) {
        if (output->newly_added) {
            if (ctx->impl && ctx->impl->new_output) {
                ctx->impl->new_output(ctx, &output->base, ctx->user_data);
            }
            output->newly_added = false;
            output->pending_mask = 0;
        } else {
            if (output->pending_mask) {
                if (output->impl && output->impl->state) {
                    output->impl->state(&output->base, output->pending_mask);
                }
                output->pending_mask = 0;
            }
        }
    }
}

void ky_output_manager_update_primary(struct ky_output_manager *manager, struct ky_output *primary)
{
    struct ky_output *old_primary = manager->primary;
    if (old_primary == primary) {
        return;
    }
    manager->primary = primary;

    if (old_primary) {
        old_primary->base.primary = false;
        old_primary->pending_mask |= KYWC_OUTPUT_STATE_PRIMARY;
    }
    if (primary) {
        primary->base.primary = true;
        primary->pending_mask |= KYWC_OUTPUT_STATE_PRIMARY;
    }
}

struct ky_output_manager *ky_output_manager_create(kywc_context *ctx)
{
    struct ky_output_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return NULL;
    }

    manager->ctx = ctx;
    wl_list_init(&manager->outputs);

    return manager;
}

void ky_output_manager_destroy(struct ky_output_manager *manager)
{
    if (!manager) {
        return;
    }

    // destroy all outputs
    struct ky_output *output, *tmp;
    wl_list_for_each_safe(output, tmp, &manager->outputs, link) {
        ky_output_destroy(output);
    }

    if (manager->destroy) {
        manager->destroy(manager);
    }

    free(manager);
}

void kywc_output_set_interface(kywc_output *output, const struct kywc_output_interface *impl)
{
    struct ky_output *ky_output = output_from_kywc_output(output);
    ky_output->impl = impl;
}

void kywc_context_for_each_output(kywc_context *ctx, kywc_output_iterator_func_t iterator,
                                  void *data)
{
    if (!ctx->output) {
        return;
    }

    struct ky_output *output;
    wl_list_for_each(output, &ctx->output->outputs, link) {
        if (iterator(&output->base, data)) {
            break;
        }
    }
}

kywc_output *kywc_context_find_output(kywc_context *ctx, const char *uuid)
{
    if (!ctx->output || !uuid) {
        return NULL;
    }

    struct ky_output *output;
    wl_list_for_each_reverse(output, &ctx->output->outputs, link) {
        if (strcmp(output->base.uuid, uuid) == 0) {
            return &output->base;
        }
    }

    return NULL;
}

void kywc_output_set_user_data(kywc_output *output, void *data)
{
    struct ky_output *ky_output = output_from_kywc_output(output);
    ky_output->user_data = data;
}

void *kywc_output_get_user_data(kywc_output *output)
{
    struct ky_output *ky_output = output_from_kywc_output(output);
    return ky_output->user_data;
}
