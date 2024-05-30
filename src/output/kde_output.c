// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include "dpms-protocol.h"
#include "kde-output-device-v2-protocol.h"
#include "kde-output-management-v2-protocol.h"
#include "kde-primary-output-v1-protocol.h"

#include "output_p.h"
#include "util/global.h"

#define OUTPUT_DEVICE_VERSION 2
#define OUTPUT_DEVICE_MODE_VERSION 1
#define OUTPUT_MANAGER_VERSION 2
#define KDE_PRIMARY_OUTPUT_VERSION 2
#define ORG_KDE_KWIN_DPMS_MANAGER_VERSION 1

struct kde_output_management {
    struct wl_display *display;
    struct wl_global *global;

    struct wl_list resources; // clients
    struct wl_list output_devices;

    /* for primary output */
    struct {
        struct wl_global *global;
        struct wl_list resources;
        struct wl_listener primary_output;
        struct kywc_output *current_primary;
    } primary_output;

    /* for dpms manager */
    struct {
        struct wl_global *global;
        struct wl_list resources;
    } dpms_manager;

    struct wl_listener new_output;
    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct kde_output_device {
    struct wl_global *global;
    struct wl_list link;
    struct wl_list clients;
    struct wl_list resources; // for dpms
    struct wl_event_source *idle_source;

    struct kywc_output *kywc_output;

    struct wl_listener on;
    struct wl_listener off;
    struct wl_listener scale;
    struct wl_listener mode;
    struct wl_listener position;
    struct wl_listener transform;
    struct wl_listener power;

    struct wl_listener destroy;
};

struct kde_output_config {
    struct kde_output_device *device;
    struct kywc_output_state pending;
    struct wl_list link;

    struct wl_listener output_destroy;
};

struct kde_output_configs {
    struct wl_resource *resource;
    struct kywc_output *pending_primary;

    struct wl_list configs;
};

struct kde_output_device_client {
    struct wl_resource *resource;
    struct wl_list link;

    struct kde_output_device *output_device;
    struct wl_list mode_resources;
};

static struct kde_output_management *management = NULL;

static void output_configure_handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct kde_output_config *config = wl_container_of(listener, config, output_destroy);

    wl_list_remove(&config->output_destroy.link);
    wl_list_remove(&config->link);

    free(config);
}

static struct kde_output_config *get_output_device_config(struct kde_output_configs *configs,
                                                          struct kde_output_device *device)
{
    struct kde_output_config *config;
    wl_list_for_each(config, &configs->configs, link) {
        if (config->device == device) {
            return config;
        }
    }

    config = calloc(1, sizeof(struct kde_output_config));
    if (!config) {
        return NULL;
    }

    config->device = device;
    /* filter changes when apply */
    config->pending = device->kywc_output->state;
    wl_list_insert(&configs->configs, &config->link);

    config->output_destroy.notify = output_configure_handle_output_destroy;
    wl_signal_add(&device->kywc_output->events.destroy, &config->output_destroy);

    return config;
}

static void output_configure_handle_enable(struct wl_client *client, struct wl_resource *resource,
                                           struct wl_resource *outputdevice, int32_t enable)
{
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);
    if (!kod_client->output_device) {
        return;
    }

    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_config *config = get_output_device_config(configs, kod_client->output_device);
    if (config) {
        config->pending.enabled = config->pending.power = enable;
    }
}

static void output_configure_handle_mode(struct wl_client *client, struct wl_resource *resource,
                                         struct wl_resource *outputdevice, struct wl_resource *mode)
{
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);
    if (!kod_client->output_device) {
        return;
    }

    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_config *config = get_output_device_config(configs, kod_client->output_device);
    if (config) {
        struct kywc_output_mode *output_mode = wl_resource_get_user_data(mode);
        config->pending.width = output_mode->width;
        config->pending.height = output_mode->height;
        config->pending.refresh = output_mode->refresh;
    }
}

static void output_configure_handle_transform(struct wl_client *client,
                                              struct wl_resource *resource,
                                              struct wl_resource *outputdevice, int32_t transform)
{
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);
    if (!kod_client->output_device) {
        return;
    }

    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_config *config = get_output_device_config(configs, kod_client->output_device);
    if (config) {
        config->pending.transform = transform;
    }
}

static void output_configure_handle_position(struct wl_client *client, struct wl_resource *resource,
                                             struct wl_resource *outputdevice, int32_t x, int32_t y)
{
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);
    if (!kod_client->output_device) {
        return;
    }

    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_config *config = get_output_device_config(configs, kod_client->output_device);
    if (config) {
        config->pending.lx = x;
        config->pending.ly = y;
    }
}

static void output_configure_handle_scale(struct wl_client *client, struct wl_resource *resource,
                                          struct wl_resource *outputdevice, wl_fixed_t scale)
{
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);
    if (!kod_client->output_device) {
        return;
    }

    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_config *config = get_output_device_config(configs, kod_client->output_device);
    if (config) {
        float output_scale = wl_fixed_to_double(scale);
        config->pending.scale = output_scale;
    }
}

static void output_configure_handle_apply(struct wl_client *client, struct wl_resource *resource)
{
    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kywc_output *primary_output = management->primary_output.current_primary;

    if (wl_list_empty(&management->output_devices) || !primary_output) {
        kywc_log(KYWC_WARN, "configuration cannot be applied");
        kde_output_configuration_v2_send_failed(resource);
        return;
    }

    struct kde_output_config *config;
    wl_list_for_each(config, &configs->configs, link) {
        struct output *output = output_from_kywc_output(config->device->kywc_output);
        output_manager_add_output_pending_state(output, &config->pending);
    }

    output_set_pending_primary(output_from_kywc_output(configs->pending_primary));

    if (!output_manager_configure_outputs()) {
        kde_output_configuration_v2_send_failed(resource);
        return;
    }

    kde_output_configuration_v2_send_applied(resource);
}

static void output_configure_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void output_configure_handle_overscan(struct wl_client *client, struct wl_resource *resource,
                                             struct wl_resource *outputdevice, uint32_t overscan)
{
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);
    if (!kod_client->output_device) {
        return;
    }

    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_config *config = get_output_device_config(configs, kod_client->output_device);
    if (config) {
        kywc_log(KYWC_DEBUG, "configure output %s overscan to: %d",
                 kod_client->output_device->kywc_output->name, overscan);
    }
}

static void output_configure_set_vrr_policy(struct wl_client *client, struct wl_resource *resource,
                                            struct wl_resource *outputdevice, uint32_t policy)
{
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);
    if (!kod_client->output_device) {
        return;
    }

    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_config *config = get_output_device_config(configs, kod_client->output_device);
    if (config) {
        kywc_log(KYWC_DEBUG, "configure output %s vrr policy to: %d",
                 kod_client->output_device->kywc_output->name, policy);
        config->pending.vrr_policy = policy;
    }
}

static void output_configure_set_rgb_range(struct wl_client *client, struct wl_resource *resource,
                                           struct wl_resource *outputdevice, uint32_t rgb_range)
{
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);
    if (!kod_client->output_device) {
        return;
    }

    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_config *config = get_output_device_config(configs, kod_client->output_device);
    if (config) {
        kywc_log(KYWC_DEBUG, "configure output %s rgb_range to: %d",
                 kod_client->output_device->kywc_output->name, rgb_range);
    }
}

static void output_configure_set_primary_output(struct wl_client *client,
                                                struct wl_resource *resource,
                                                struct wl_resource *outputdevice)
{
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);
    if (!kod_client->output_device) {
        return;
    }

    kywc_log(KYWC_DEBUG, "configure primary output to: %s",
             kod_client->output_device->kywc_output->name);

    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    uint32_t version = wl_resource_get_version(resource);
    if (version >= KDE_OUTPUT_CONFIGURATION_V2_SET_PRIMARY_OUTPUT_SINCE_VERSION) {
        configs->pending_primary = kod_client->output_device->kywc_output;
    }
}

static const struct kde_output_configuration_v2_interface kde_output_configure_impl = {
    .enable = output_configure_handle_enable,
    .mode = output_configure_handle_mode,
    .transform = output_configure_handle_transform,
    .position = output_configure_handle_position,
    .scale = output_configure_handle_scale,
    .apply = output_configure_handle_apply,
    .destroy = output_configure_handle_destroy,
    .overscan = output_configure_handle_overscan,
    .set_vrr_policy = output_configure_set_vrr_policy,
    .set_rgb_range = output_configure_set_rgb_range,
    .set_primary_output = output_configure_set_primary_output,
};

static void kde_output_configs_handle_resource_destroy(struct wl_resource *resource)
{
    struct kde_output_configs *configs = wl_resource_get_user_data(resource);

    struct kde_output_config *config, *config_tmp;
    wl_list_for_each_safe(config, config_tmp, &configs->configs, link) {
        wl_list_remove(&config->output_destroy.link);
        wl_list_remove(&config->link);
        free(config);
    }

    free(configs);
}

static void output_management_handle_create_configure(
    struct wl_client *client, struct wl_resource *output_management_resource, uint32_t id)
{
    struct kde_output_configs *configs = calloc(1, sizeof(struct kde_output_configs));
    if (!configs) {
        wl_client_post_no_memory(client);
        return;
    }

    uint32_t version = wl_resource_get_version(output_management_resource);
    struct wl_resource *resource =
        wl_resource_create(client, &kde_output_configuration_v2_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        free(configs);
        return;
    }

    configs->resource = resource;
    wl_list_init(&configs->configs);
    configs->pending_primary = management->primary_output.current_primary;

    wl_resource_set_implementation(resource, &kde_output_configure_impl, configs,
                                   kde_output_configs_handle_resource_destroy);
}

static const struct kde_output_management_v2_interface kde_output_management_impl = {
    .create_configuration = output_management_handle_create_configure,
};

static void kde_output_management_handle_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static void kde_output_management_bind(struct wl_client *client, void *data, uint32_t version,
                                       uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &kde_output_management_v2_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &kde_output_management_impl, management,
                                   kde_output_management_handle_resource_destroy);
    wl_list_insert(&management->resources, wl_resource_get_link(resource));
}

static void kde_output_management_handle_server_destory(struct wl_listener *listener, void *data)
{
    wl_list_remove(&management->server_destroy.link);

    free(management);
    management = NULL;
}

static void kde_output_management_handle_display_destory(struct wl_listener *listener, void *data)
{
    wl_list_remove(&management->display_destroy.link);
    wl_list_remove(&management->new_output.link);

    wl_global_destroy(management->global);

    if (management->primary_output.global) {
        wl_list_remove(&management->primary_output.primary_output.link);
        wl_global_destroy(management->primary_output.global);
    }

    if (management->dpms_manager.global) {
        wl_global_destroy(management->dpms_manager.global);
    }
}

static void kde_output_device_handle_mode_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static void kde_output_device_send_modes(struct kde_output_device_client *kod_client)
{
    struct wl_client *client = wl_resource_get_client(kod_client->resource);
    struct kywc_output *kywc_output = kod_client->output_device->kywc_output;

    struct kywc_output_mode *mode;
    wl_list_for_each(mode, &kywc_output->prop.modes, link) {
        struct wl_resource *mode_resource = wl_resource_create(
            client, &kde_output_device_mode_v2_interface, OUTPUT_DEVICE_MODE_VERSION, 0);
        if (!mode_resource) {
            continue;
        }

        wl_list_insert(&kod_client->mode_resources, wl_resource_get_link(mode_resource));

        /* for current_mode and mode config */
        wl_resource_set_user_data(mode_resource, mode);
        wl_resource_set_destructor(mode_resource, kde_output_device_handle_mode_resource_destroy);

        kde_output_device_v2_send_mode(kod_client->resource, mode_resource);
        kde_output_device_mode_v2_send_size(mode_resource, mode->width, mode->height);
        kde_output_device_mode_v2_send_refresh(mode_resource, mode->refresh);
        if (mode->preferred) {
            kde_output_device_mode_v2_send_preferred(mode_resource);
        }
    }
}

static void kde_output_device_send_current_mode(struct kde_output_device_client *kod_client)
{
    struct kywc_output *kywc_output = kod_client->output_device->kywc_output;

    /* only send if output is enabled */
    if (!kywc_output->state.enabled) {
        return;
    }

    struct wl_resource *mode_resource;
    wl_resource_for_each(mode_resource, &kod_client->mode_resources) {
        struct kywc_output_mode *mode = wl_resource_get_user_data(mode_resource);
        if (mode->width != kywc_output->state.width || mode->height != kywc_output->state.height ||
            mode->refresh != kywc_output->state.refresh) {
            continue;
        }

        kde_output_device_v2_send_current_mode(kod_client->resource, mode_resource);
        break;
    }
}

static void kde_output_device_unbind(struct wl_resource *resource)
{
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(resource);

    struct wl_resource *mode_resource, *tmp;
    wl_resource_for_each_safe(mode_resource, tmp, &kod_client->mode_resources) {
        wl_list_remove(wl_resource_get_link(mode_resource));
        wl_list_init(wl_resource_get_link(mode_resource));
    }

    wl_list_remove(&kod_client->link);
    free(kod_client);
}

static void kde_output_device_bind(struct wl_client *client, void *data, uint32_t version,
                                   uint32_t id)
{
    struct kde_output_device *output_device = data;
    if (!output_device) {
        return;
    }

    struct kywc_output *kywc_output = output_device->kywc_output;

    struct kde_output_device_client *kod_client =
        calloc(1, sizeof(struct kde_output_device_client));
    if (!kod_client) {
        wl_client_post_no_memory(client);
        return;
    }

    struct wl_resource *resource =
        wl_resource_create(client, &kde_output_device_v2_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        free(kod_client);
        return;
    }

    kod_client->resource = resource;
    kod_client->output_device = output_device;
    wl_list_init(&kod_client->mode_resources);

    /* user_data used in configuration */
    wl_resource_set_user_data(resource, kod_client);
    wl_resource_set_destructor(resource, kde_output_device_unbind);
    wl_list_insert(&output_device->clients, &kod_client->link);

    kde_output_device_v2_send_enabled(resource, kywc_output->state.enabled);
    kde_output_device_v2_send_geometry(resource, kywc_output->state.lx, kywc_output->state.ly,
                                       kywc_output->prop.phys_width, kywc_output->prop.phys_height,
                                       0, kywc_output->prop.make, kywc_output->prop.model,
                                       kywc_output->state.transform);

    kde_output_device_send_modes(kod_client);
    kde_output_device_send_current_mode(kod_client);

    kde_output_device_v2_send_scale(resource, wl_fixed_from_double(kywc_output->state.scale));

    kde_output_device_v2_send_serial_number(
        resource, kywc_output->prop.serial ? kywc_output->prop.serial : "");
    kde_output_device_v2_send_uuid(resource, kywc_output->uuid);
    kde_output_device_v2_send_edid(resource, kywc_output->edid ? kywc_output->edid : "");

    /* TODO: finish these
        kde_output_device_v2_send_eisa_id();
        kde_output_device_v2_send_capabilities();
        kde_output_device_v2_send_overscan();
        kde_output_device_v2_send_vrr_policy();
        kde_output_device_v2_send_rgb_range();
    */
    if (version >= KDE_OUTPUT_DEVICE_V2_NAME_SINCE_VERSION) {
        kde_output_device_v2_send_name(resource, kywc_output->name);
    }

    kde_output_device_v2_send_done(resource);
}

static void kde_output_device_handle_destroy(struct wl_listener *listener, void *data)
{
    struct kde_output_device *output_device = wl_container_of(listener, output_device, destroy);

    wl_list_remove(&output_device->on.link);
    wl_list_remove(&output_device->off.link);
    wl_list_remove(&output_device->mode.link);
    wl_list_remove(&output_device->scale.link);
    wl_list_remove(&output_device->transform.link);
    wl_list_remove(&output_device->position.link);
    wl_list_remove(&output_device->destroy.link);
    wl_list_remove(&output_device->link);

    struct kde_output_device_client *client, *client_tmp;
    wl_list_for_each_safe(client, client_tmp, &output_device->clients, link) {
        wl_list_remove(&client->link);
        wl_list_init(&client->link);
        client->output_device = NULL;
    }

    struct wl_resource *resource, *tmp;
    wl_resource_for_each_safe(resource, tmp, &output_device->resources) {
        wl_list_remove(wl_resource_get_link(resource));
        wl_list_init(wl_resource_get_link(resource));
        wl_resource_set_user_data(resource, NULL);
    }

    if (output_device->idle_source) {
        wl_event_source_remove(output_device->idle_source);
    }

    /* global destroy when output destroy */
    wl_global_destroy_safe(output_device->global);

    free(output_device);
}

static void output_idle_send_done(void *data)
{
    struct kde_output_device *output_device = data;

    struct kde_output_device_client *client;
    wl_list_for_each(client, &output_device->clients, link) {
        kde_output_device_v2_send_done(client->resource);
    }

    output_device->idle_source = NULL;
}

static void output_update_idle_source(struct kde_output_device *output_device)
{
    if (output_device->idle_source) {
        return;
    }

    struct wl_event_loop *loop = wl_display_get_event_loop(management->display);
    output_device->idle_source = wl_event_loop_add_idle(loop, output_idle_send_done, output_device);
}

static void kde_output_device_handle_on(struct wl_listener *listener, void *data)
{
    struct kde_output_device *output_device = wl_container_of(listener, output_device, on);

    struct kde_output_device_client *client;
    wl_list_for_each(client, &output_device->clients, link) {
        kde_output_device_v2_send_enabled(client->resource, true);
        kde_output_device_send_current_mode(client);
    }

    output_update_idle_source(output_device);
}

static void kde_output_device_handle_off(struct wl_listener *listener, void *data)
{
    struct kde_output_device *output_device = wl_container_of(listener, output_device, off);

    struct kde_output_device_client *client;
    wl_list_for_each(client, &output_device->clients, link) {
        kde_output_device_v2_send_enabled(client->resource, false);
    }

    output_update_idle_source(output_device);
}

static void kde_output_device_handle_mode(struct wl_listener *listener, void *data)
{
    struct kde_output_device *output_device = wl_container_of(listener, output_device, mode);

    struct kde_output_device_client *client;
    wl_list_for_each(client, &output_device->clients, link) {
        kde_output_device_send_current_mode(client);
    }

    output_update_idle_source(output_device);
}

static void kde_output_device_handle_scale(struct wl_listener *listener, void *data)
{
    struct kde_output_device *output_device = wl_container_of(listener, output_device, scale);

    struct kde_output_device_client *client;
    wl_list_for_each(client, &output_device->clients, link) {
        kde_output_device_v2_send_scale(
            client->resource, wl_fixed_from_double(output_device->kywc_output->state.scale));
    }

    output_update_idle_source(output_device);
}

static void kde_output_device_handle_position(struct wl_listener *listener, void *data)
{
    struct kde_output_device *output_device = wl_container_of(listener, output_device, position);
    struct kywc_output *kywc_output = output_device->kywc_output;

    struct kde_output_device_client *client;
    wl_list_for_each(client, &output_device->clients, link) {
        kde_output_device_v2_send_geometry(client->resource, kywc_output->state.lx,
                                           kywc_output->state.ly, kywc_output->prop.phys_width,
                                           kywc_output->prop.phys_height, 0, kywc_output->prop.make,
                                           kywc_output->prop.model, kywc_output->state.transform);
    }

    output_update_idle_source(output_device);
}

static void kde_output_device_handle_transform(struct wl_listener *listener, void *data)
{
    struct kde_output_device *output_device = wl_container_of(listener, output_device, transform);
    struct kywc_output *kywc_output = output_device->kywc_output;

    struct kde_output_device_client *client;
    wl_list_for_each(client, &output_device->clients, link) {
        kde_output_device_v2_send_geometry(client->resource, kywc_output->state.lx,
                                           kywc_output->state.ly, kywc_output->prop.phys_width,
                                           kywc_output->prop.phys_height, 0, kywc_output->prop.make,
                                           kywc_output->prop.model, kywc_output->state.transform);
    }

    output_update_idle_source(output_device);
}

static void kde_output_device_handle_power(struct wl_listener *listener, void *data)
{
    struct kde_output_device *output_device = wl_container_of(listener, output_device, power);
    struct kywc_output *kywc_output = output_device->kywc_output;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &output_device->resources) {
        org_kde_kwin_dpms_send_mode(resource, kywc_output->state.power
                                                  ? ORG_KDE_KWIN_DPMS_MODE_ON
                                                  : ORG_KDE_KWIN_DPMS_MODE_OFF);
        org_kde_kwin_dpms_send_done(resource);
    }
}

#define OUTPUT_DEVICE_ADD_SIGNAL(signal)                                                           \
    output_device->signal.notify = kde_output_device_handle_##signal;                              \
    wl_signal_add(&kywc_output->events.signal, &output_device->signal);

static void kde_output_management_handle_new_output(struct wl_listener *listener, void *data)
{
    struct kywc_output *kywc_output = data;
    if (kywc_output->prop.is_virtual) {
        return;
    }

    struct kde_output_device *output_device = calloc(1, sizeof(struct kde_output_device));
    if (!output_device) {
        return;
    }

    output_device->global =
        wl_global_create(management->display, &kde_output_device_v2_interface,
                         OUTPUT_DEVICE_VERSION, output_device, kde_output_device_bind);
    if (!output_device->global) {
        free(output_device);
        return;
    }

    wl_list_init(&output_device->clients);
    wl_list_init(&output_device->resources);
    wl_list_insert(&management->output_devices, &output_device->link);

    output_device->kywc_output = kywc_output;

    OUTPUT_DEVICE_ADD_SIGNAL(on);
    OUTPUT_DEVICE_ADD_SIGNAL(off);
    OUTPUT_DEVICE_ADD_SIGNAL(mode);
    OUTPUT_DEVICE_ADD_SIGNAL(scale);
    OUTPUT_DEVICE_ADD_SIGNAL(position);
    OUTPUT_DEVICE_ADD_SIGNAL(transform);
    OUTPUT_DEVICE_ADD_SIGNAL(power);
    OUTPUT_DEVICE_ADD_SIGNAL(destroy);
}

#undef OUTPUT_DEVICE_ADD_SIGNAL

static void kde_primary_output_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct kde_primary_output_v1_interface kde_primary_output_impl = {
    .destroy = kde_primary_output_destroy,
};

static void kde_primary_output_unbind(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static void kde_primary_output_bind(struct wl_client *client, void *data, uint32_t version,
                                    uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &kde_primary_output_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &kde_primary_output_impl, NULL,
                                   kde_primary_output_unbind);
    wl_list_insert(&management->primary_output.resources, wl_resource_get_link(resource));

    struct kywc_output *primary = management->primary_output.current_primary;
    if (primary) {
        kde_primary_output_v1_send_primary_output(resource, primary->name);
    }
}

static void kde_output_management_handle_primary_output(struct wl_listener *listener, void *data)
{
    struct kywc_output *kywc_output = data;
    management->primary_output.current_primary = kywc_output;

    if (!kywc_output) {
        return;
    }

    struct wl_resource *resource;
    wl_resource_for_each(resource, &management->primary_output.resources) {
        kde_primary_output_v1_send_primary_output(resource, kywc_output->name);
    }
}

static void kde_dpms_set(struct wl_client *client, struct wl_resource *resource, uint32_t mode)
{
    struct kde_output_device *output_device = wl_resource_get_user_data(resource);
    if (!output_device) {
        return;
    }

    /* dpms protocol uses wl_output, so output must be enabled */
    struct kywc_output_state state = output_device->kywc_output->state;
    state.power = mode == ORG_KDE_KWIN_DPMS_MODE_OFF ? false : true;

    kywc_output_set_state(output_device->kywc_output, &state);
    output_manager_emit_configured(CONFIGURE_TYPE_NONE);
}

static void kde_dpms_release(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct org_kde_kwin_dpms_interface kde_dpms_impl = {
    .set = kde_dpms_set,
    .release = kde_dpms_release,
};

static void kde_dpms_handle_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static struct kde_output_device *output_device_from_resource(struct wl_resource *resource)
{
    struct output *output = output_from_resource(resource);
    if (!output) {
        return NULL;
    }

    struct kde_output_device *output_device;
    wl_list_for_each(output_device, &management->output_devices, link) {
        if (output_device->kywc_output == &output->base) {
            return output_device;
        }
    }

    return NULL;
}

static void kde_dpms_manager_get(struct wl_client *client, struct wl_resource *manager_resource,
                                 uint32_t id, struct wl_resource *output_resource)
{
    /* get output from output resource */
    struct kde_output_device *output_device = output_device_from_resource(output_resource);
    if (!output_device) {
        wl_client_post_implementation_error(client, "invalid output");
        return;
    }

    uint32_t version = wl_resource_get_version(manager_resource);
    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_kwin_dpms_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &kde_dpms_impl, output_device,
                                   kde_dpms_handle_resource_destroy);
    wl_list_insert(&output_device->resources, wl_resource_get_link(resource));

    /* send current dpms state */
    org_kde_kwin_dpms_send_supported(resource, true);
    org_kde_kwin_dpms_send_mode(resource, output_device->kywc_output->state.power
                                              ? ORG_KDE_KWIN_DPMS_MODE_ON
                                              : ORG_KDE_KWIN_DPMS_MODE_OFF);
    org_kde_kwin_dpms_send_done(resource);
}

static const struct org_kde_kwin_dpms_manager_interface kde_dpms_manager_impl = {
    .get = kde_dpms_manager_get,
};

static void kde_dpms_manager_handle_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static void kde_dpms_manager_bind(struct wl_client *client, void *data, uint32_t version,
                                  uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_kwin_dpms_manager_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &kde_dpms_manager_impl, management,
                                   kde_dpms_manager_handle_resource_destroy);
    wl_list_insert(&management->dpms_manager.resources, wl_resource_get_link(resource));
}

bool kde_output_management_create(struct server *server)
{
    management = calloc(1, sizeof(struct kde_output_management));
    if (!management) {
        return false;
    }

    management->display = server->display;
    wl_list_init(&management->resources);
    wl_list_init(&management->output_devices);

    management->global =
        wl_global_create(server->display, &kde_output_management_v2_interface,
                         OUTPUT_MANAGER_VERSION, management, kde_output_management_bind);
    if (!management->global) {
        free(management);
        return false;
    }

    management->display_destroy.notify = kde_output_management_handle_display_destory;
    wl_display_add_destroy_listener(server->display, &management->display_destroy);
    management->server_destroy.notify = kde_output_management_handle_server_destory;
    server_add_destroy_listener(server, &management->server_destroy);

    /* listener new_output signal */
    management->new_output.notify = kde_output_management_handle_new_output;
    kywc_output_add_new_listener(&management->new_output);

    /* kde_primary_output_v1 support */
    management->primary_output.global =
        wl_global_create(server->display, &kde_primary_output_v1_interface,
                         KDE_PRIMARY_OUTPUT_VERSION, management, kde_primary_output_bind);
    if (management->primary_output.global) {
        wl_list_init(&management->primary_output.resources);
        /* listener primary_output signal */
        management->primary_output.primary_output.notify =
            kde_output_management_handle_primary_output;
        kywc_output_add_primary_listener(&management->primary_output.primary_output);
    } else {
        kywc_log(KYWC_WARN, "%s global create failed", kde_primary_output_v1_interface.name);
    }

    /* org_kde_kwin_dpms_manager suppport */
    management->dpms_manager.global =
        wl_global_create(server->display, &org_kde_kwin_dpms_manager_interface,
                         ORG_KDE_KWIN_DPMS_MANAGER_VERSION, management, kde_dpms_manager_bind);
    if (management->dpms_manager.global) {
        wl_list_init(&management->dpms_manager.resources);
    } else {
        kywc_log(KYWC_WARN, "%s global create failed", org_kde_kwin_dpms_manager_interface.name);
    }

    return true;
}
