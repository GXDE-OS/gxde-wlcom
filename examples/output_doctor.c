// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dpms-client-protocol.h"
#include "kde-output-device-v2-client-protocol.h"
#include "kde-output-management-v2-client-protocol.h"
#include "kde-primary-output-v1-client-protocol.h"

/**
 * some code is from wlr-randr.
 */

struct output_manager {
    struct kde_output_management_v2 *management;
    struct kde_primary_output_v1 *primary;
    struct org_kde_kwin_dpms_manager *dpms;
    struct wl_list devices;
    struct wl_list outputs;
    char *primary_output_name;
    struct output_device *pending_primary;
    int32_t pending_configs;
};

struct output_device_state {
    bool enabled;
    int32_t x, y;
    double scale;
    uint32_t overscan;
    enum kde_output_device_v2_transform transform;
    enum kde_output_device_v2_vrr_policy vrr_policy;
    enum kde_output_device_v2_rgb_range rgb_range;
    struct output_device_mode *mode;
    // primary in output_manager
};

struct output_device {
    struct output_manager *manager;
    struct kde_output_device_v2 *device;
    struct wl_list link;
    struct wl_list modes;

    char *name, *uuid;
    char *make, *model, *serial_number;
    char *edid, *eisa_id;
    int32_t phys_width, phys_height;
    enum kde_output_device_v2_subpixel subpixel;
    uint32_t capabilities;

    struct output_device_state current, pending;

    uint32_t global_name;
};

struct output_device_mode {
    struct kde_output_device_mode_v2 *mode;
    struct output_device *device;
    struct wl_list link;

    int32_t width, height, refresh;
    bool preferred;
};

struct output {
    struct output_manager *manager;
    struct wl_output *wl_output;
    struct org_kde_kwin_dpms *dpms;
    struct wl_list link;

    char *name, *model;
    bool support_dpms;
    enum org_kde_kwin_dpms_mode dpms_mode;

    bool have_pending_dpms;
    enum org_kde_kwin_dpms_mode pending_dpms_mode;

    uint32_t global_name;
    uint32_t version;
};

static const char *output_subpixel_map[] = {
    [KDE_OUTPUT_DEVICE_V2_SUBPIXEL_UNKNOWN] = "unknown",
    [KDE_OUTPUT_DEVICE_V2_SUBPIXEL_NONE] = "none",
    [KDE_OUTPUT_DEVICE_V2_SUBPIXEL_HORIZONTAL_RGB] = "horizontal-rgb",
    [KDE_OUTPUT_DEVICE_V2_SUBPIXEL_HORIZONTAL_BGR] = "horizontal-bgr",
    [KDE_OUTPUT_DEVICE_V2_SUBPIXEL_VERTICAL_RGB] = "vertical-rgb",
    [KDE_OUTPUT_DEVICE_V2_SUBPIXEL_VERTICAL_BGR] = "vertical-bgr",
};

static const char *output_transform_map[] = {
    [KDE_OUTPUT_DEVICE_V2_TRANSFORM_NORMAL] = "normal",
    [KDE_OUTPUT_DEVICE_V2_TRANSFORM_90] = "90",
    [KDE_OUTPUT_DEVICE_V2_TRANSFORM_180] = "180",
    [KDE_OUTPUT_DEVICE_V2_TRANSFORM_270] = "270",
    [KDE_OUTPUT_DEVICE_V2_TRANSFORM_FLIPPED] = "flipped",
    [KDE_OUTPUT_DEVICE_V2_TRANSFORM_FLIPPED_90] = "flipped-90",
    [KDE_OUTPUT_DEVICE_V2_TRANSFORM_FLIPPED_180] = "flipped-180",
    [KDE_OUTPUT_DEVICE_V2_TRANSFORM_FLIPPED_270] = "flipped-270",
};

static const char *output_vrr_policy_map[] = {
    [KDE_OUTPUT_DEVICE_V2_VRR_POLICY_NEVER] = "never",
    [KDE_OUTPUT_DEVICE_V2_VRR_POLICY_ALWAYS] = "always",
    [KDE_OUTPUT_DEVICE_V2_VRR_POLICY_AUTOMATIC] = "automatic",
};

static const char *output_rgb_range_map[] = {
    [KDE_OUTPUT_DEVICE_V2_RGB_RANGE_AUTOMATIC] = "automatic",
    [KDE_OUTPUT_DEVICE_V2_RGB_RANGE_FULL] = "full",
    [KDE_OUTPUT_DEVICE_V2_RGB_RANGE_LIMITED] = "limited",
};

static void print_output_state(struct output_device *output_device)
{
    struct output_device_state *current = &output_device->current;

    if (kde_output_device_v2_get_version(output_device->device) >=
        KDE_OUTPUT_DEVICE_V2_NAME_SINCE_VERSION) {
        printf("%s \"%s\"\n", output_device->name, output_device->uuid);
    } else {
        printf("%s\n", output_device->uuid);
    }

    printf("  Make: %s\n", output_device->make);
    printf("  Model: %s\n", output_device->model);
    printf("  Serial: %s\n", output_device->serial_number);

    if (output_device->phys_width > 0 && output_device->phys_height > 0) {
        printf("  Physical size: %dx%d mm\n", output_device->phys_width,
               output_device->phys_height);
    }

    printf("  Enabled: %s\n", current->enabled ? "yes" : "no");

    if (!wl_list_empty(&output_device->modes)) {
        printf("  Modes:\n");
        struct output_device_mode *mode;
        wl_list_for_each(mode, &output_device->modes, link) {
            printf("    %dx%d px", mode->width, mode->height);
            if (mode->refresh > 0) {
                printf(", %f Hz", (float)mode->refresh / 1000);
            }
            bool match = current->mode == mode;
            if (match || mode->preferred) {
                printf(" (");
                if (mode->preferred) {
                    printf("preferred");
                }
                if (match && mode->preferred) {
                    printf(", ");
                }
                if (match) {
                    printf("current");
                }
                printf(")");
            }
            printf("\n");
        }
    }

    if (!current->enabled) {
        return;
    }

    printf("  Position: %d,%d\n", current->x, current->y);
    printf("  Subpixel: %s\n", output_subpixel_map[output_device->subpixel]);
    printf("  Transform: %s\n", output_transform_map[current->transform]);
    printf("  Scale: %f\n", current->scale);

    if (output_device->capabilities & KDE_OUTPUT_DEVICE_V2_CAPABILITY_OVERSCAN) {
        printf("  Overscan: %d%%\n", current->overscan);
    }
    if (output_device->capabilities & KDE_OUTPUT_DEVICE_V2_CAPABILITY_VRR) {
        printf("  VRR policy: %s\n", output_vrr_policy_map[current->vrr_policy]);
    }
    if (output_device->capabilities & KDE_OUTPUT_DEVICE_V2_CAPABILITY_RGB_RANGE) {
        printf("  RGB range: %s\n", output_rgb_range_map[current->rgb_range]);
    }
}

static void print_state(struct output_manager *output_manager)
{
    struct output_device *output_device;
    wl_list_for_each(output_device, &output_manager->devices, link) {
        print_output_state(output_device);
    }

    output_manager->pending_configs--;
}

static void output_config_applied(void *data,
                                  struct kde_output_configuration_v2 *kde_output_configuration_v2)
{
    struct output_manager *output_manager = data;
    kde_output_configuration_v2_destroy(kde_output_configuration_v2);
    output_manager->pending_configs--;

    fprintf(stderr, "succeed to apply configuration\n");
}

static void output_config_failed(void *data,
                                 struct kde_output_configuration_v2 *kde_output_configuration_v2)
{
    struct output_manager *output_manager = data;
    kde_output_configuration_v2_destroy(kde_output_configuration_v2);
    output_manager->pending_configs--;

    fprintf(stderr, "failed to apply configuration\n");
}

static const struct kde_output_configuration_v2_listener output_config_listener = {
    .applied = output_config_applied,
    .failed = output_config_failed,
};

static void apply_state(struct output_manager *output_manager)
{
    struct kde_output_configuration_v2 *config =
        kde_output_management_v2_create_configuration(output_manager->management);
    kde_output_configuration_v2_add_listener(config, &output_config_listener, output_manager);

    bool config_need_applied = false;
    struct output_device_state *current, *pending;
    struct output_device *output_device;
    wl_list_for_each(output_device, &output_manager->devices, link) {
        current = &output_device->current;
        pending = &output_device->pending;

        if (current->enabled && !pending->enabled) {
            config_need_applied = true;
            kde_output_configuration_v2_enable(config, output_device->device, false);
            continue;
        }

        if (!current->enabled && pending->enabled) {
            config_need_applied = true;
            kde_output_configuration_v2_enable(config, output_device->device, true);
        }
        if (current->mode != pending->mode) {
            config_need_applied = true;
            kde_output_configuration_v2_mode(config, output_device->device, pending->mode->mode);
        }
        if (current->transform != pending->transform) {
            config_need_applied = true;
            kde_output_configuration_v2_transform(config, output_device->device,
                                                  pending->transform);
        }
        if (current->x != pending->x || current->y != pending->y) {
            config_need_applied = true;
            kde_output_configuration_v2_position(config, output_device->device, pending->x,
                                                 pending->y);
        }
        if (current->scale != pending->scale) {
            config_need_applied = true;
            kde_output_configuration_v2_scale(config, output_device->device,
                                              wl_fixed_from_double(pending->scale));
        }
        if ((output_device->capabilities & KDE_OUTPUT_DEVICE_V2_CAPABILITY_OVERSCAN) &&
            current->overscan != pending->overscan) {
            config_need_applied = true;
            kde_output_configuration_v2_overscan(config, output_device->device, pending->overscan);
        }
        if ((output_device->capabilities & KDE_OUTPUT_DEVICE_V2_CAPABILITY_VRR) &&
            current->vrr_policy != pending->vrr_policy) {
            config_need_applied = true;
            kde_output_configuration_v2_set_vrr_policy(config, output_device->device,
                                                       pending->vrr_policy);
        }
        if ((output_device->capabilities & KDE_OUTPUT_DEVICE_V2_CAPABILITY_RGB_RANGE) &&
            current->rgb_range != pending->rgb_range) {
            config_need_applied = true;
            kde_output_configuration_v2_set_rgb_range(config, output_device->device,
                                                      pending->rgb_range);
        }
    }

    if (kde_output_configuration_v2_get_version(config) >=
            KDE_OUTPUT_CONFIGURATION_V2_SET_PRIMARY_OUTPUT_SINCE_VERSION &&
        output_manager->pending_primary &&
        (!output_manager->primary_output_name || // primary output may be null
         strcmp(output_manager->primary_output_name, output_manager->pending_primary->name))) {
        config_need_applied = true;
        kde_output_configuration_v2_set_primary_output(config,
                                                       output_manager->pending_primary->device);
    }

    if (config_need_applied) {
        kde_output_configuration_v2_apply(config);
    }

    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (!output->have_pending_dpms) {
            continue;
        }

        if (output->dpms_mode != output->pending_dpms_mode) {
            output_manager->pending_configs++;
            org_kde_kwin_dpms_set(output->dpms, output->pending_dpms_mode);
        }
    }

    if (!config_need_applied) {
        output_manager->pending_configs--;
        kde_output_configuration_v2_destroy(config);
    }
}

static void output_device_mode_size(void *data,
                                    struct kde_output_device_mode_v2 *kde_output_device_mode_v2,
                                    int32_t width, int32_t height)
{
    struct output_device_mode *output_device_mode = data;
    output_device_mode->width = width;
    output_device_mode->height = height;
}

static void output_device_mode_refresh(void *data,
                                       struct kde_output_device_mode_v2 *kde_output_device_mode_v2,
                                       int32_t refresh)
{
    struct output_device_mode *output_device_mode = data;
    output_device_mode->refresh = refresh;
}

static void
output_device_mode_preferred(void *data,
                             struct kde_output_device_mode_v2 *kde_output_device_mode_v2)
{
    struct output_device_mode *output_device_mode = data;
    output_device_mode->preferred = true;
}

static void output_device_mode_removed(void *data,
                                       struct kde_output_device_mode_v2 *kde_output_device_mode_v2)
{
    struct output_device_mode *output_device_mode = data;
    wl_list_remove(&output_device_mode->link);
    // TODO: remove current mode ?
    free(output_device_mode);
}

static const struct kde_output_device_mode_v2_listener output_device_mode_listener = {
    .size = output_device_mode_size,
    .refresh = output_device_mode_refresh,
    .preferred = output_device_mode_preferred,
    .removed = output_device_mode_removed,
};

static void output_device_mode(void *data, struct kde_output_device_v2 *kde_output_device_v2,
                               struct kde_output_device_mode_v2 *mode)
{
    struct output_device_mode *output_device_mode = calloc(1, sizeof(struct output_device_mode));
    if (!output_device_mode) {
        return;
    }

    struct output_device *output_device = data;
    output_device_mode->device = output_device;
    output_device_mode->mode = mode;
    wl_list_insert(&output_device->modes, &output_device_mode->link);

    kde_output_device_mode_v2_add_listener(mode, &output_device_mode_listener, output_device_mode);
    kde_output_device_mode_v2_set_user_data(mode, output_device_mode);
}

static void output_device_geometry(void *data, struct kde_output_device_v2 *kde_output_device_v2,
                                   int32_t x, int32_t y, int32_t physical_width,
                                   int32_t physical_height, int32_t subpixel, const char *make,
                                   const char *model, int32_t transform)
{
    struct output_device *output_device = data;

    output_device->current.x = x;
    output_device->current.y = y;
    output_device->current.transform = transform;

    output_device->phys_width = physical_width;
    output_device->phys_height = physical_height;
    output_device->subpixel = subpixel;

    free(output_device->make);
    output_device->make = strdup(make);
    free(output_device->model);
    output_device->model = strdup(model);
}

static void output_device_current_mode(void *data,
                                       struct kde_output_device_v2 *kde_output_device_v2,
                                       struct kde_output_device_mode_v2 *mode)
{
    struct output_device *output_device = data;
    struct output_device_mode *output_device_mode = kde_output_device_mode_v2_get_user_data(mode);
    output_device->current.mode = output_device_mode;
}

static void output_device_done(void *data, struct kde_output_device_v2 *kde_output_device_v2)
{
    struct output_device *output_device = data;
    output_device->pending = output_device->current;
}

static void output_device_scale(void *data, struct kde_output_device_v2 *kde_output_device_v2,
                                wl_fixed_t factor)
{
    struct output_device *output_device = data;
    output_device->current.scale = wl_fixed_to_double(factor);
}

static void output_device_edid(void *data, struct kde_output_device_v2 *kde_output_device_v2,
                               const char *raw)
{
    struct output_device *output_device = data;
    free(output_device->edid);
    output_device->edid = strdup(raw);
}

static void output_device_enabled(void *data, struct kde_output_device_v2 *kde_output_device_v2,
                                  int32_t enabled)
{
    struct output_device *output_device = data;
    output_device->current.enabled = enabled;
}

static void output_device_uuid(void *data, struct kde_output_device_v2 *kde_output_device_v2,
                               const char *uuid)
{
    struct output_device *output_device = data;
    free(output_device->uuid);
    output_device->uuid = strdup(uuid);
}

static void output_device_serial_number(void *data,
                                        struct kde_output_device_v2 *kde_output_device_v2,
                                        const char *serial_number)
{
    struct output_device *output_device = data;
    free(output_device->serial_number);
    output_device->serial_number = strdup(serial_number);
}

static void output_device_eisa_id(void *data, struct kde_output_device_v2 *kde_output_device_v2,
                                  const char *eisa_id)
{
    struct output_device *output_device = data;
    free(output_device->eisa_id);
    output_device->eisa_id = strdup(eisa_id);
}

static void output_device_capabilities(void *data,
                                       struct kde_output_device_v2 *kde_output_device_v2,
                                       uint32_t flags)
{
    struct output_device *output_device = data;
    output_device->capabilities = flags;
}

static void output_device_overscan(void *data, struct kde_output_device_v2 *kde_output_device_v2,
                                   uint32_t overscan)
{
    struct output_device *output_device = data;
    output_device->current.overscan = overscan;
}

static void output_device_vrr_policy(void *data, struct kde_output_device_v2 *kde_output_device_v2,
                                     uint32_t vrr_policy)
{
    struct output_device *output_device = data;
    output_device->current.vrr_policy = vrr_policy;
}

static void output_device_rgb_range(void *data, struct kde_output_device_v2 *kde_output_device_v2,
                                    uint32_t rgb_range)
{
    struct output_device *output_device = data;
    output_device->current.rgb_range = rgb_range;
}

static void output_device_name(void *data, struct kde_output_device_v2 *kde_output_device_v2,
                               const char *name)
{
    struct output_device *output_device = data;
    free(output_device->name);
    output_device->name = strdup(name);
}

static const struct kde_output_device_v2_listener output_device_listener = {
    .geometry = output_device_geometry,
    .current_mode = output_device_current_mode,
    .mode = output_device_mode,
    .done = output_device_done,
    .scale = output_device_scale,
    .edid = output_device_edid,
    .enabled = output_device_enabled,
    .uuid = output_device_uuid,
    .serial_number = output_device_serial_number,
    .eisa_id = output_device_eisa_id,
    .capabilities = output_device_capabilities,
    .overscan = output_device_overscan,
    .vrr_policy = output_device_vrr_policy,
    .rgb_range = output_device_rgb_range,
    .name = output_device_name,
};

static void primary_output_primary(void *data, struct kde_primary_output_v1 *kde_primary_output_v1,
                                   const char *output_name)
{
    struct output_manager *output_manager = data;

    free(output_manager->primary_output_name);
    output_manager->primary_output_name = strdup(output_name);
    printf("Primary output: %s\n", output_name);
}

static const struct kde_primary_output_v1_listener primary_output_listener = {
    .primary_output = primary_output_primary,
};

static void output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y,
                            int32_t physical_width, int32_t physical_height, int32_t subpixel,
                            const char *make, const char *model, int32_t transform)
{
    struct output *output = data;
    free(output->model);
    output->model = strdup(model);
}

static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width,
                        int32_t height, int32_t refresh)
{
    // This space intentionally left blank
}

static void output_done(void *data, struct wl_output *wl_output)
{
    // This space intentionally left blank
}

static void output_scale(void *data, struct wl_output *wl_output, int32_t factor)
{
    // This space intentionally left blank
}

static void output_name(void *data, struct wl_output *wl_output, const char *name)
{
    struct output *output = data;
    free(output->name);
    output->name = strdup(name);
}

static void output_description(void *data, struct wl_output *wl_output, const char *description)
{
    // This space intentionally left blank
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

static void output_dpms_supported(void *data, struct org_kde_kwin_dpms *org_kde_kwin_dpms,
                                  uint32_t supported)
{
    struct output *output = data;
    output->support_dpms = supported;
}

static void output_dpms_mode(void *data, struct org_kde_kwin_dpms *org_kde_kwin_dpms, uint32_t mode)
{
    struct output *output = data;
    output->dpms_mode = mode;
}

static void output_dpms_done(void *data, struct org_kde_kwin_dpms *org_kde_kwin_dpms)
{
    struct output *output = data;

    if (output->have_pending_dpms) {
        output->manager->pending_configs--;
    }
}

static const struct org_kde_kwin_dpms_listener output_dpms_listener = {
    .supported = output_dpms_supported,
    .mode = output_dpms_mode,
    .done = output_dpms_done,
};

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version)
{
    struct output_manager *output_manager = data;
    uint32_t version_to_bind;
    // printf("global %s (id = %d version = %d)\n", interface, name, version);

    if (!strcmp(interface, kde_output_management_v2_interface.name)) {
        version_to_bind = version <= 2 ? version : 2;
        output_manager->management =
            wl_registry_bind(registry, name, &kde_output_management_v2_interface, version_to_bind);
    } else if (!strcmp(interface, kde_output_device_v2_interface.name)) {
        struct output_device *output_device = calloc(1, sizeof(struct output_device));
        if (!output_device) {
            return;
        }

        output_device->global_name = name;
        output_device->manager = output_manager;
        wl_list_init(&output_device->modes);
        wl_list_insert(&output_manager->devices, &output_device->link);

        version_to_bind = version <= 2 ? version : 2;
        output_device->device =
            wl_registry_bind(registry, name, &kde_output_device_v2_interface, version_to_bind);
        kde_output_device_v2_add_listener(output_device->device, &output_device_listener,
                                          output_device);
    } else if (!strcmp(interface, kde_primary_output_v1_interface.name)) {
        output_manager->primary =
            wl_registry_bind(registry, name, &kde_primary_output_v1_interface, 2);
        kde_primary_output_v1_add_listener(output_manager->primary, &primary_output_listener,
                                           output_manager);
    } else if (!strcmp(interface, org_kde_kwin_dpms_manager_interface.name)) {
        output_manager->dpms =
            wl_registry_bind(registry, name, &org_kde_kwin_dpms_manager_interface, 1);

        struct output *output;
        wl_list_for_each(output, &output_manager->outputs, link) {
            output->dpms = org_kde_kwin_dpms_manager_get(output_manager->dpms, output->wl_output);
            org_kde_kwin_dpms_add_listener(output->dpms, &output_dpms_listener, output);
        }
    } else if (!strcmp(interface, wl_output_interface.name)) {
        struct output *output = calloc(1, sizeof(struct output));
        if (!output) {
            return;
        }

        output->manager = output_manager;
        output->global_name = name;
        wl_list_insert(&output_manager->outputs, &output->link);

        output->version = version <= 4 ? version : 4;
        output->wl_output = wl_registry_bind(registry, name, &wl_output_interface, output->version);
        wl_output_add_listener(output->wl_output, &output_listener, output);

        if (output_manager->dpms) {
            output->dpms = org_kde_kwin_dpms_manager_get(output_manager->dpms, output->wl_output);
            org_kde_kwin_dpms_add_listener(output->dpms, &output_dpms_listener, output);
        }
    }
}

static void output_device_destroy(struct output_device *output_device)
{
    wl_list_remove(&output_device->link);

    struct output_device_mode *mode, *mode_tmp;
    wl_list_for_each_safe(mode, mode_tmp, &output_device->modes, link) {
        wl_list_remove(&mode->link);
        kde_output_device_mode_v2_destroy(mode->mode);
        free(mode);
    }

    kde_output_device_v2_destroy(output_device->device);

    free(output_device->name);
    free(output_device->uuid);
    free(output_device->make);
    free(output_device->model);
    free(output_device->serial_number);
    free(output_device->edid);
    free(output_device->eisa_id);
    free(output_device);
}

static void output_destroy(struct output *output)
{
    wl_list_remove(&output->link);

    if (output->dpms) {
        org_kde_kwin_dpms_release(output->dpms);
    }

    if (output->version >= WL_OUTPUT_RELEASE_SINCE_VERSION) {
        wl_output_release(output->wl_output);
    } else {
        wl_output_destroy(output->wl_output);
    }

    free(output->name);
    free(output->model);
    free(output);
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    // output device destroy when plug out
    struct output_manager *output_manager = data;

    struct output_device *output_device, *output_device_tmp;
    wl_list_for_each_safe(output_device, output_device_tmp, &output_manager->devices, link) {
        if (output_device->global_name == name) {
            output_device_destroy(output_device);
            return;
        }
    }

    struct output *output, *output_tmp;
    wl_list_for_each_safe(output, output_tmp, &output_manager->outputs, link) {
        if (output->global_name == name) {
            output_destroy(output);
            return;
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static const struct option long_options[] = {
    { "help", no_argument, 0, 'h' },
    { "monitor", no_argument, 0, 0 },
    { "output", required_argument, 0, 0 },
    { "on", no_argument, 0, 0 },
    { "off", no_argument, 0, 0 },
    { "mode", required_argument, 0, 0 },
    { "primary", no_argument, 0, 0 },
    { "preferred", no_argument, 0, 0 },
    { "pos", required_argument, 0, 0 },
    { "transform", required_argument, 0, 0 },
    { "scale", required_argument, 0, 0 },
    { "overscan", required_argument, 0, 0 },
    { "vrr-policy", required_argument, 0, 0 },
    { "rgb-range", required_argument, 0, 0 },
    { "dpms", required_argument, 0, 0 },
    { 0 },
};

static bool parse_mode(const char *value, int *width, int *height, int *refresh)
{
    *refresh = 0;

    // width + "x" + height
    char *cur = (char *)value;
    char *end;
    *width = strtol(cur, &end, 10);
    if (end[0] != 'x' || cur == end) {
        fprintf(stderr, "invalid mode: invalid width: %s\n", value);
        return false;
    }

    cur = end + 1;
    *height = strtol(cur, &end, 10);
    if (cur == end) {
        fprintf(stderr, "invalid mode: invalid height: %s\n", value);
        return false;
    }
    if (end[0] != '\0') {
        // whitespace + "px"
        cur = end;
        while (cur[0] == ' ') {
            cur++;
        }
        if (strncmp(cur, "px", 2) == 0) {
            cur += 2;
        }

        if (cur[0] != '\0') {
            // ("," or "@") + whitespace + refresh
            if (cur[0] == ',' || cur[0] == '@') {
                cur++;
            } else {
                fprintf(stderr, "invalid mode: expected refresh rate: %s\n", value);
                return false;
            }
            while (cur[0] == ' ') {
                cur++;
            }
            double refresh_hz = strtod(cur, &end);
            if ((end[0] != '\0' && strcmp(end, "Hz") != 0) || cur == end || refresh_hz <= 0) {
                fprintf(stderr, "invalid mode: invalid refresh rate: %s\n", value);
                return false;
            }

            *refresh = refresh_hz * 1000; // Hz → mHz
        }
    }

    return true;
}

static void fixup_disabled_device(struct output_device *device)
{
    if (device->pending.mode) {
        return;
    }

    struct output_device_mode *mode;
    wl_list_for_each(mode, &device->modes, link) {
        if (mode->preferred) {
            device->pending.mode = mode;
            return;
        }
    }
    /* Pick first element if when there's no preferred mode */
    if (!wl_list_empty(&device->modes)) {
        device->pending.mode = wl_container_of(device->modes.next, mode, link);
    }
}

static bool parse_output_arg(struct output_device *device, struct output *output, const char *name,
                             const char *value)
{
    if (strcmp(name, "on") == 0) {
        if (!device->current.enabled) {
            fixup_disabled_device(device);
        }
        device->pending.enabled = true;
    } else if (strcmp(name, "off") == 0) {
        device->pending.enabled = false;
    } else if (strcmp(name, "mode") == 0) {
        int width, height, refresh;
        if (!parse_mode(value, &width, &height, &refresh)) {
            return false;
        }

        bool found = false;
        struct output_device_mode *mode;
        wl_list_for_each(mode, &device->modes, link) {
            if (mode->width == width && mode->height == height &&
                (refresh == 0 || mode->refresh == refresh)) {
                found = true;
                break;
            }
        }

        if (!found) {
            fprintf(stderr, "unknown mode: %s\n", value);
            return false;
        }

        device->pending.mode = mode;
    } else if (strcmp(name, "preferred") == 0) {
        bool found = false;
        struct output_device_mode *mode;
        wl_list_for_each(mode, &device->modes, link) {
            if (mode->preferred) {
                found = true;
                break;
            }
        }

        if (!found) {
            fprintf(stderr, "no preferred mode found\n");
            return false;
        }

        device->pending.mode = mode;
    } else if (strcmp(name, "pos") == 0) {
        char *cur = (char *)value;
        char *end;
        int x = strtol(cur, &end, 10);
        if (end[0] != ',' || cur == end) {
            fprintf(stderr, "invalid position: %s\n", value);
            return false;
        }

        cur = end + 1;
        int y = strtol(cur, &end, 10);
        if (end[0] != '\0') {
            fprintf(stderr, "invalid position: %s\n", value);
            return false;
        }

        device->pending.x = x;
        device->pending.y = y;
    } else if (strcmp(name, "transform") == 0) {
        bool found = false;
        size_t len = sizeof(output_transform_map) / sizeof(output_transform_map[0]);
        for (size_t i = 0; i < len; ++i) {
            if (strcmp(output_transform_map[i], value) == 0) {
                found = true;
                device->pending.transform = i;
                break;
            }
        }

        if (!found) {
            fprintf(stderr, "invalid transform: %s\n", value);
            return false;
        }
    } else if (strcmp(name, "scale") == 0) {
        char *end;
        double scale = strtod(value, &end);
        if (end[0] != '\0' || value == end) {
            fprintf(stderr, "invalid scale: %s\n", value);
            return false;
        }

        device->pending.scale = scale;
    } else if (strcmp(name, "overscan") == 0) {
        char *end;
        uint32_t overscan = strtol(value, &end, 10);
        if (end[0] != '\0' || value == end) {
            fprintf(stderr, "invalid overscan: %s\n", value);
            return false;
        }

        device->pending.overscan = overscan;
    } else if (strcmp(name, "vrr-policy") == 0) {
        if (strcmp(value, "never") == 0) {
            device->pending.vrr_policy = KDE_OUTPUT_DEVICE_V2_VRR_POLICY_NEVER;
        } else if (strcmp(value, "always") == 0) {
            device->pending.vrr_policy = KDE_OUTPUT_DEVICE_V2_VRR_POLICY_ALWAYS;
        } else if (strcmp(value, "automatic") == 0) {
            device->pending.vrr_policy = KDE_OUTPUT_DEVICE_V2_VRR_POLICY_AUTOMATIC;
        } else {
            fprintf(stderr, "invalid vrr policy: %s\n", value);
            return false;
        }
    } else if (strcmp(name, "rgb-range") == 0) {
        if (strcmp(value, "automatic") == 0) {
            device->pending.rgb_range = KDE_OUTPUT_DEVICE_V2_RGB_RANGE_AUTOMATIC;
        } else if (strcmp(value, "full") == 0) {
            device->pending.rgb_range = KDE_OUTPUT_DEVICE_V2_RGB_RANGE_FULL;
        } else if (strcmp(value, "limited") == 0) {
            device->pending.rgb_range = KDE_OUTPUT_DEVICE_V2_RGB_RANGE_LIMITED;
        } else {
            fprintf(stderr, "invalid rgb range: %s\n", value);
            return false;
        }
    } else if (strcmp(name, "primary") == 0) {
        device->manager->pending_primary = device;
    } else if (strcmp(name, "dpms") == 0) {
        if (!output) {
            fprintf(stderr, "output %s is not found for dpms, skip\n", device->name);
            return true;
        }
        if (!output->support_dpms) {
            fprintf(stderr, "output %s is not support dpms, skip\n", device->name);
            return true;
        }

        if (strcmp(value, "on") == 0) {
            output->pending_dpms_mode = ORG_KDE_KWIN_DPMS_MODE_ON;
        } else if (strcmp(value, "off") == 0) {
            output->pending_dpms_mode = ORG_KDE_KWIN_DPMS_MODE_OFF;
        } else {
            fprintf(stderr, "invalid dpms state: %s\n", value);
            return false;
        }
        output->have_pending_dpms = true;
    } else {
        fprintf(stderr, "invalid option: %s\n", name);
        return false;
    }

    return true;
}

static const char usage[] =
    "usage: output-doctor [options…]\n\n"
    " --help\n"
    " --monitor\n"
    " --output <name>\n"
    "   --on\n"
    "   --off\n"
    "   --dpms on|off\n"
    "   --mode <width>x<height>[@<refresh>Hz]\n"
    "   --primary\n"
    "   --preferred\n"
    "   --pos <x>,<y>\n"
    "   --transform normal|90|180|270|flipped|flipped-90|flipped-180|flipped-270\n"
    "   --scale <factor>\n"
    "   --overscan <percent>\n"
    "   --vrr-policy never|always|automatic\n"
    "   --rgb-range automatic|full|limited\n";

int main(int argc, char *argv[])
{
    struct output_manager manager = { .pending_configs = 1 };
    wl_list_init(&manager.devices);
    wl_list_init(&manager.outputs);

    struct wl_display *display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "failed to connect to display\n");
        return EXIT_FAILURE;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &manager);
    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    if (!manager.management) {
        fprintf(stderr, "compositor doesn't support kde-output-management-v2\n");
        return EXIT_FAILURE;
    }

    // XXX: make sure we have get all outputs

    bool changed = false;
    struct output_device *current_device = NULL;
    struct output *current_output = NULL;
    while (1) {
        int option_index = -1;
        int c = getopt_long(argc, argv, "h", long_options, &option_index);
        if (c < 0) {
            break;
        } else if (c == '?') {
            goto done;
        } else if (c == 'h') {
            fprintf(stderr, "%s", usage);
            goto done;
        }

        const char *name = long_options[option_index].name;
        const char *value = optarg;
        if (strcmp(name, "output") == 0) {
            bool found = false;
            wl_list_for_each(current_device, &manager.devices, link) {
                if (strcmp(current_device->name, value) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "unknown output %s\n", value);
                goto done;
            }

            found = false;
            wl_list_for_each(current_output, &manager.outputs, link) {
                if (strcmp(current_output->name, value) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                current_output = NULL;
            }
        } else if (strcmp(name, "monitor") == 0) {
            manager.pending_configs++;
        } else { // output sub-option
            if (current_device == NULL) {
                fprintf(stderr, "no --output specified before --%s\n", name);
                goto done;
            }

            if (!parse_output_arg(current_device, current_output, name, value)) {
                goto done;
            }

            changed = true;
        }
    }

    if (changed) {
        apply_state(&manager);
    } else {
        print_state(&manager);
    }

    while (manager.pending_configs && wl_display_dispatch(display) != -1) {
        // This space intentionally left blank
    }

    struct output_device *output_device, *output_device_tmp;
done:
    wl_list_for_each_safe(output_device, output_device_tmp, &manager.devices, link) {
        output_device_destroy(output_device);
    }

    struct output *output, *output_tmp;
    wl_list_for_each_safe(output, output_tmp, &manager.outputs, link) {
        output_destroy(output);
    }
    if (manager.dpms) {
        org_kde_kwin_dpms_manager_destroy(manager.dpms);
    }
    kde_primary_output_v1_destroy(manager.primary);
    kde_output_management_v2_destroy(manager.management);
    free(manager.primary_output_name);

    wl_registry_destroy(registry);
    wl_display_flush(display);
    wl_display_disconnect(display);

    return 0;
}
