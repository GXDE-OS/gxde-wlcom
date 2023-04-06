#include <stdlib.h>

#include "kde-output-device-v2-protocol.h"
#include "kde-output-management-v2-protocol.h"
#include "kde-primary-output-v1-protocol.h"

#include "output_p.h"

#define OUTPUT_DEVICE_VERSION 2
#define OUTPUT_DEVICE_MODE_VERSION 1
#define OUTPUT_MANAGER_VERSION 2
#define KDE_PRIMARY_OUTPUT_VERSION 1

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

    struct wl_listener new_output;
    struct wl_listener display_destroy;
};

struct kde_output_device {
    struct wl_global *global;
    struct wl_list link;
    struct wl_list clients;

    struct kywc_output *kywc_output;

    struct wl_listener on;
    struct wl_listener off;
    struct wl_listener scale;
    struct wl_listener mode;
    struct wl_listener position;
    struct wl_listener transform;

    struct wl_listener destroy;
};

struct kde_output_config {
    struct kde_output_device *device;
    struct kywc_output_state pending;
    struct wl_list link;
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

static struct kde_output_config *configs_get_output_device(struct kde_output_configs *configs,
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
    return config;
}

static void output_configure_handle_enable(struct wl_client *client, struct wl_resource *resource,
                                           struct wl_resource *outputdevice, int32_t enable)
{
    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);

    if (configs && kod_client && kod_client->output_device) {
        struct kde_output_config *config =
            configs_get_output_device(configs, kod_client->output_device);
        if (config) {
            config->pending.enabled = enable;
        }
    }
}

static void output_configure_handle_mode(struct wl_client *client, struct wl_resource *resource,
                                         struct wl_resource *outputdevice, struct wl_resource *mode)
{
    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);

    if (configs && kod_client && kod_client->output_device) {
        struct kde_output_config *config =
            configs_get_output_device(configs, kod_client->output_device);
        if (config) {
            struct kywc_output_mode *output_mode = wl_resource_get_user_data(mode);
            config->pending.width = output_mode->width;
            config->pending.height = output_mode->height;
            config->pending.refresh = output_mode->refresh;
        }
    }
}

static void output_configure_handle_transform(struct wl_client *client,
                                              struct wl_resource *resource,
                                              struct wl_resource *outputdevice, int32_t transform)
{
    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);

    if (configs && kod_client && kod_client->output_device) {
        struct kde_output_config *config =
            configs_get_output_device(configs, kod_client->output_device);
        if (config) {
            config->pending.transform = transform;
        }
    }
}

static void output_configure_handle_position(struct wl_client *client, struct wl_resource *resource,
                                             struct wl_resource *outputdevice, int32_t x, int32_t y)
{
    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);

    if (configs && kod_client && kod_client->output_device) {
        struct kde_output_config *config =
            configs_get_output_device(configs, kod_client->output_device);
        if (config) {
            config->pending.lx = x;
            config->pending.ly = y;
        }
    }
}

static void output_configure_handle_scale(struct wl_client *client, struct wl_resource *resource,
                                          struct wl_resource *outputdevice, wl_fixed_t scale)
{
    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);

    if (configs && kod_client && kod_client->output_device) {
        struct kde_output_config *config =
            configs_get_output_device(configs, kod_client->output_device);
        if (config) {
            float output_scale = wl_fixed_to_double(scale);
            config->pending.scale = output_scale;
        }
    }
}

static struct kde_output_config *
output_device_config_in_configs(struct kde_output_configs *configs,
                                struct kde_output_device *output_device)
{
    struct kde_output_config *config;
    wl_list_for_each(config, &configs->configs, link) {
        if (output_device == config->device) {
            return config;
        }
    }

    return NULL;
}

static void output_configure_handle_apply(struct wl_client *client, struct wl_resource *resource)
{
    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kywc_output *primary_output = management->primary_output.current_primary;

    /* primary output may be disabled, fixup it */
    bool need_fix_primary_output = !configs->pending_primary->state.enabled;

    struct kywc_output *kywc_output;
    struct kde_output_config *config;
    wl_list_for_each(config, &configs->configs, link) {
        kywc_output = config->device->kywc_output;
        /* It's going to disable the primary output and no new primary config */
        if (kywc_output->state.enabled && !config->pending.enabled &&
            configs->pending_primary == primary_output && kywc_output == primary_output) {
            need_fix_primary_output = true;
            break;
        }
    }

    /* if all outputs will be disabled in config, find others not in config */
    bool have_enabled_output = false;
    struct kde_output_device *output_device;
    wl_list_for_each(output_device, &management->output_devices, link) {
        config = output_device_config_in_configs(configs, output_device);
        if (config) {
            have_enabled_output |= config->pending.enabled;
        } else {
            have_enabled_output |= output_device->kywc_output->state.enabled;
        }
        if (!have_enabled_output) {
            continue;
        }

        /* fixup primary output */
        if (need_fix_primary_output) {
            kywc_log(KYWC_WARN, "Fixup primary output to %s", output_device->kywc_output->name);
            configs->pending_primary = output_device->kywc_output;
        }
        break;
    }

    if (!have_enabled_output) {
        kywc_log(KYWC_WARN, "All outputs will be disabled, reject this configuration");
        kde_output_configuration_v2_send_failed(resource);
        return;
    }

    kywc_output_set_primary(configs->pending_primary);

    /* call kywc_output_set_state in all outputs */
    wl_list_for_each(config, &configs->configs, link) {
        if (!kywc_output_set_state(config->device->kywc_output, &config->pending)) {
            kde_output_configuration_v2_send_failed(resource);
            return;
        }
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
    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);

    if (configs && kod_client && kod_client->output_device) {
        struct kde_output_config *config =
            configs_get_output_device(configs, kod_client->output_device);
        if (config) {
            kywc_log(KYWC_DEBUG, "configure output %s overscan to: %d",
                     kod_client->output_device->kywc_output->name, overscan);
        }
    }
}

static void output_configure_set_vrr_policy(struct wl_client *client, struct wl_resource *resource,
                                            struct wl_resource *outputdevice, uint32_t policy)
{
    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);

    if (configs && kod_client && kod_client->output_device) {
        struct kde_output_config *config =
            configs_get_output_device(configs, kod_client->output_device);
        if (config) {
            kywc_log(KYWC_DEBUG, "configure output %s vrr policy to: %d",
                     kod_client->output_device->kywc_output->name, policy);
            config->pending.vrr_policy = policy;
        }
    }
}

static void output_configure_set_rgb_range(struct wl_client *client, struct wl_resource *resource,
                                           struct wl_resource *outputdevice, uint32_t rgb_range)
{
    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);

    if (configs && kod_client && kod_client->output_device) {
        struct kde_output_config *config =
            configs_get_output_device(configs, kod_client->output_device);
        if (config) {
            kywc_log(KYWC_DEBUG, "configure output %s rgb_range to: %d",
                     kod_client->output_device->kywc_output->name, rgb_range);
        }
    }
}

static void output_configure_set_primary_output(struct wl_client *client,
                                                struct wl_resource *resource,
                                                struct wl_resource *outputdevice)
{
    struct kde_output_configs *configs = wl_resource_get_user_data(resource);
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(outputdevice);

    if (configs && kod_client && kod_client->output_device) {
        kywc_log(KYWC_DEBUG, "configure primary output to: %s",
                 kod_client->output_device->kywc_output->name);

        uint32_t version = wl_resource_get_version(resource);
        if (version >= KDE_OUTPUT_CONFIGURATION_V2_SET_PRIMARY_OUTPUT_SINCE_VERSION) {
            configs->pending_primary = kod_client->output_device->kywc_output;
        }
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

static void kde_output_management_handle_display_destory(struct wl_listener *listener, void *data)
{
    wl_list_remove(&management->display_destroy.link);
    // XXX: can't remove other links as we used display_destroy

    wl_global_destroy(management->global);
    if (management->primary_output.global) {
        wl_global_destroy(management->primary_output.global);
    }

    free(management);
    management = NULL;
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

    struct wl_resource *mode_resource;
    // TODO: optimize this
    wl_list_for_each_reverse(mode_resource, &kod_client->mode_resources, link) {
        struct kywc_output_mode *mode = wl_resource_get_user_data(mode_resource);
        if (mode->width != kywc_output->state.width || mode->height != kywc_output->state.height ||
            mode->refresh != kywc_output->state.refresh) {
            continue;
        }

        /* only send if output is enabled */
        if (kywc_output->state.enabled) {
            kde_output_device_v2_send_current_mode(kod_client->resource, mode_resource);
        }
        break;
    }
}

static void kde_output_device_unbind(struct wl_resource *resource)
{
    struct kde_output_device_client *kod_client = wl_resource_get_user_data(resource);
    wl_list_remove(&kod_client->link);
    free(kod_client);
}

static void kde_output_device_bind(struct wl_client *client, void *data, uint32_t version,
                                   uint32_t id)
{
    struct kde_output_device *output_device = data;
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

    /* TODO: finish these
        kde_output_device_v2_send_edid();
        kde_output_device_v2_send_uuid();
        kde_output_device_v2_send_serial_number();
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

    wl_list_remove(&output_device->destroy.link);
    wl_list_remove(&output_device->link);

    /* global destroy when output destroy */
    wl_global_destroy(output_device->global);

    free(output_device);
}

static void kde_output_device_handle_on(struct wl_listener *listener, void *data)
{
    struct kde_output_device *output_device = wl_container_of(listener, output_device, on);

    struct kde_output_device_client *client;
    wl_list_for_each(client, &output_device->clients, link) {
        kde_output_device_v2_send_enabled(client->resource, true);
        kde_output_device_send_current_mode(client);
        kde_output_device_v2_send_done(client->resource);
    }
}

static void kde_output_device_handle_off(struct wl_listener *listener, void *data)
{
    struct kde_output_device *output_device = wl_container_of(listener, output_device, off);

    struct kde_output_device_client *client;
    wl_list_for_each(client, &output_device->clients, link) {
        kde_output_device_v2_send_enabled(client->resource, false);
        kde_output_device_v2_send_done(client->resource);
    }
}

static void kde_output_device_handle_mode(struct wl_listener *listener, void *data)
{
    struct kde_output_device *output_device = wl_container_of(listener, output_device, mode);

    struct kde_output_device_client *client;
    wl_list_for_each(client, &output_device->clients, link) {
        kde_output_device_send_current_mode(client);
        kde_output_device_v2_send_done(client->resource);
    }
}

static void kde_output_device_handle_scale(struct wl_listener *listener, void *data)
{
    struct kde_output_device *output_device = wl_container_of(listener, output_device, scale);

    struct kde_output_device_client *client;
    wl_list_for_each(client, &output_device->clients, link) {
        kde_output_device_v2_send_scale(
            client->resource, wl_fixed_from_double(output_device->kywc_output->state.scale));
        kde_output_device_v2_send_done(client->resource);
    }
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
        kde_output_device_v2_send_done(client->resource);
    }
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
        kde_output_device_v2_send_done(client->resource);
    }
}

#define OUTPUT_DEVICE_ADD_SIGNAL(signal)                                                           \
    output_device->signal.notify = kde_output_device_handle_##signal;                              \
    wl_signal_add(&kywc_output->events.signal, &output_device->signal);

static void kde_output_management_handle_new_output(struct wl_listener *listener, void *data)
{
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
    wl_list_insert(&management->output_devices, &output_device->link);

    struct kywc_output *kywc_output = data;
    output_device->kywc_output = kywc_output;

    OUTPUT_DEVICE_ADD_SIGNAL(on);
    OUTPUT_DEVICE_ADD_SIGNAL(off);
    OUTPUT_DEVICE_ADD_SIGNAL(mode);
    OUTPUT_DEVICE_ADD_SIGNAL(scale);
    OUTPUT_DEVICE_ADD_SIGNAL(position);
    OUTPUT_DEVICE_ADD_SIGNAL(transform);
    OUTPUT_DEVICE_ADD_SIGNAL(destroy);
}

#undef OUTPUT_DEVICE_ADD_SIGNAL

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

    wl_resource_set_destructor(resource, kde_primary_output_unbind);
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

    struct wl_resource *resource;
    wl_resource_for_each(resource, &management->primary_output.resources) {
        kde_primary_output_v1_send_primary_output(resource, kywc_output->name);
    }
}

bool kde_output_management_create(struct wl_display *display)
{
    management = calloc(1, sizeof(struct kde_output_management));
    if (!management) {
        return false;
    }

    management->display = display;
    wl_list_init(&management->resources);
    wl_list_init(&management->output_devices);

    management->global =
        wl_global_create(display, &kde_output_management_v2_interface, OUTPUT_MANAGER_VERSION,
                         management, kde_output_management_bind);
    if (!management->global) {
        free(management);
        return false;
    }

    /* listener new_output signal */
    management->new_output.notify = kde_output_management_handle_new_output;
    kywc_output_add_new_listener(&management->new_output);

    /* kde_primary_output_v1 support */
    management->primary_output.global =
        wl_global_create(display, &kde_primary_output_v1_interface, KDE_PRIMARY_OUTPUT_VERSION,
                         management, kde_primary_output_bind);
    if (!management->primary_output.global) {
        /* no primary output support */
        return true;
    }

    wl_list_init(&management->primary_output.resources);
    /* listener primary_output signal */
    management->primary_output.primary_output.notify = kde_output_management_handle_primary_output;
    kywc_output_add_primary_listener(&management->primary_output.primary_output);

    // TODO: check with server_destroy
    management->display_destroy.notify = kde_output_management_handle_display_destory;
    wl_display_add_destroy_listener(display, &management->display_destroy);

    return true;
}
