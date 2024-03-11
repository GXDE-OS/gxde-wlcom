// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include "kywc-output-v1-protocol.h"

#include "output_p.h"

struct ky_output_manager {
    struct wl_global *global;
    struct wl_event_loop *event_loop;
    struct wl_event_source *idle_source;

    struct wl_list resources;
    struct wl_list outputs;
    struct ky_output *primary;

    struct wl_listener new_output;
    struct wl_listener primary_output;
    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct ky_output {
    struct ky_output_manager *manager;
    struct wl_list link;

    struct wl_list clients; // ky_output_client.link

    struct kywc_output *kywc_output;

    struct wl_listener on;
    struct wl_listener off;
    struct wl_listener scale;
    struct wl_listener mode;
    struct wl_listener position;
    struct wl_listener transform;
    struct wl_listener power;
    struct wl_listener brightness;
    struct wl_listener color_temp;

    struct wl_listener destroy;
};

// used to manage modes for output per client
struct ky_output_client {
    struct wl_resource *resource;
    struct wl_list link;

    struct ky_output *output;
    struct wl_list mode_resources;
};

static struct ky_output *ky_output_from_kywc_output(struct ky_output_manager *manager,
                                                    struct kywc_output *kywc_output)
{
    struct ky_output *ky_output;
    wl_list_for_each(ky_output, &manager->outputs, link) {
        if (ky_output->kywc_output == kywc_output) {
            return ky_output;
        }
    }
    return NULL;
}

static void output_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct kywc_output_v1_interface ky_output_impl = {
    .destroy = output_handle_destroy,
};

static void ky_output_resource_destroy(struct wl_resource *resource)
{
    struct ky_output_client *ky_client = wl_resource_get_user_data(resource);
    if (ky_client) {
        wl_list_remove(&ky_client->link);
        free(ky_client);
    }
}

static struct ky_output_client *
create_output_client_for_resource(struct ky_output *ky_output, struct wl_resource *manager_resource)
{
    struct ky_output_client *ky_client = calloc(1, sizeof(*ky_client));
    if (!ky_client) {
        return NULL;
    }

    struct wl_client *client = wl_resource_get_client(manager_resource);
    struct wl_resource *resource = wl_resource_create(client, &kywc_output_v1_interface,
                                                      wl_resource_get_version(manager_resource), 0);
    if (!resource) {
        wl_client_post_no_memory(client);
        free(ky_client);
        return NULL;
    }

    ky_client->resource = resource;
    ky_client->output = ky_output;
    wl_list_init(&ky_client->mode_resources);
    wl_list_insert(&ky_output->clients, &ky_client->link);

    wl_resource_set_implementation(resource, &ky_output_impl, ky_client,
                                   ky_output_resource_destroy);

    kywc_output_manager_v1_send_output(manager_resource, resource, ky_output->kywc_output->uuid);

    return ky_client;
}

static void output_mode_handle_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static void output_mode_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct kywc_output_mode_v1_interface output_mode_impl = {
    .destroy = output_mode_handle_destroy,
};

static void send_modes_to_output_client(struct ky_output_client *ky_client)
{
    struct wl_client *client = wl_resource_get_client(ky_client->resource);
    struct kywc_output *kywc_output = ky_client->output->kywc_output;
    uint32_t version = wl_resource_get_version(ky_client->resource);

    struct kywc_output_mode *mode;
    wl_list_for_each(mode, &kywc_output->prop.modes, link) {
        struct wl_resource *mode_resource =
            wl_resource_create(client, &kywc_output_mode_v1_interface, version, 0);
        if (!mode_resource) {
            continue;
        }

        wl_resource_set_implementation(mode_resource, &output_mode_impl, mode,
                                       output_mode_handle_resource_destroy);
        wl_list_insert(&ky_client->mode_resources, wl_resource_get_link(mode_resource));

        kywc_output_v1_send_mode(ky_client->resource, mode_resource);
        kywc_output_mode_v1_send_size(mode_resource, mode->width, mode->height);
        kywc_output_mode_v1_send_refresh(mode_resource, mode->refresh);
        if (mode->preferred) {
            kywc_output_mode_v1_send_preferred(mode_resource);
        }
    }
}

static void send_current_mode_to_output_client(struct ky_output_client *ky_client)
{
    struct kywc_output *kywc_output = ky_client->output->kywc_output;
    /* only send if output is enabled */
    if (!kywc_output->state.enabled) {
        return;
    }

    struct wl_resource *mode_resource;
    wl_list_for_each_reverse(mode_resource, &ky_client->mode_resources, link) {
        struct kywc_output_mode *mode = wl_resource_get_user_data(mode_resource);
        if (mode->width != kywc_output->state.width || mode->height != kywc_output->state.height ||
            mode->refresh != kywc_output->state.refresh) {
            continue;
        }

        kywc_output_v1_send_current_mode(ky_client->resource, mode_resource);
        break;
    }
}

static void send_details_to_output_client(struct ky_output_client *client)
{
    if (!client) {
        return;
    }

    struct kywc_output *kywc_output = client->output->kywc_output;

    kywc_output_v1_send_name(client->resource, kywc_output->name);
    if (kywc_output->prop.make) {
        kywc_output_v1_send_make(client->resource, kywc_output->prop.make);
    }
    if (kywc_output->prop.model) {
        kywc_output_v1_send_model(client->resource, kywc_output->prop.model);
    }
    if (kywc_output->prop.serial) {
        kywc_output_v1_send_serial_number(client->resource, kywc_output->prop.serial);
    }
    kywc_output_v1_send_description(client->resource, kywc_output->prop.desc);
    kywc_output_v1_send_physical_size(client->resource, kywc_output->prop.phys_width,
                                      kywc_output->prop.phys_height);
    kywc_output_v1_send_capabilities(client->resource, kywc_output->prop.capabilities);

    send_modes_to_output_client(client);
    send_current_mode_to_output_client(client);

    kywc_output_v1_send_enabled(client->resource, kywc_output->state.enabled);
    kywc_output_v1_send_position(client->resource, kywc_output->state.lx, kywc_output->state.ly);
    kywc_output_v1_send_transform(client->resource, kywc_output->state.transform);
    kywc_output_v1_send_scale(client->resource, wl_fixed_from_double(kywc_output->state.scale));
    if (kywc_output->prop.capabilities & KYWC_OUTPUT_CAPABILITY_POWER) {
        kywc_output_v1_send_power(client->resource, kywc_output->state.power);
    }
    if (kywc_output->prop.capabilities & KYWC_OUTPUT_CAPABILITY_BRIGHTNESS) {
        kywc_output_v1_send_brightness(client->resource, kywc_output->state.brightness);
    }
    if (kywc_output->prop.capabilities & KYWC_OUTPUT_CAPABILITY_COLOR_TEMP) {
        kywc_output_v1_send_color_temp(client->resource, kywc_output->state.color_temp);
    }
}

static struct ky_output_client *output_client_find_for_client(struct ky_output *ky_output,
                                                              struct wl_client *client)
{
    struct ky_output_client *ky_client;
    wl_list_for_each(ky_client, &ky_output->clients, link) {
        if (client == wl_resource_get_client(ky_client->resource)) {
            return ky_client;
        }
    }
    return NULL;
}

static void manager_handle_stop(struct wl_client *client, struct wl_resource *resource)
{
    kywc_output_manager_v1_send_finished(resource);
    wl_resource_destroy(resource);
}

static const struct kywc_output_manager_v1_interface ky_output_manager_impl = {
    .stop = manager_handle_stop,
};

static void manager_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static void ky_output_manager_bind(struct wl_client *client, void *data, uint32_t version,
                                   uint32_t id)
{
    struct ky_output_manager *manager = data;
    struct wl_resource *resource =
        wl_resource_create(client, &kywc_output_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &ky_output_manager_impl, manager,
                                   manager_resource_destroy);
    wl_list_insert(&manager->resources, wl_resource_get_link(resource));

    /* send all outputs and details */
    struct ky_output *ky_output, *tmp;
    wl_list_for_each_reverse_safe(ky_output, tmp, &manager->outputs, link) {
        create_output_client_for_resource(ky_output, resource);
    }
    struct ky_output_client *ky_client;
    wl_list_for_each_reverse_safe(ky_output, tmp, &manager->outputs, link) {
        ky_client = output_client_find_for_client(ky_output, client);
        send_details_to_output_client(ky_client);
    }
    if (manager->primary) {
        ky_client = output_client_find_for_client(manager->primary, client);
        kywc_output_manager_v1_send_primary(resource, ky_client->resource);
    }
    kywc_output_manager_v1_send_done(resource);
}

static void manager_idle_send_done(void *data)
{
    struct ky_output_manager *manager = data;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &manager->resources) {
        kywc_output_manager_v1_send_done(resource);
    }

    manager->idle_source = NULL;
}

static void manager_update_idle_source(struct ky_output_manager *manager)
{
    if (manager->idle_source) {
        return;
    }

    manager->idle_source =
        wl_event_loop_add_idle(manager->event_loop, manager_idle_send_done, manager);
}

static void handle_output_on(struct wl_listener *listener, void *data)
{
    struct ky_output *ky_output = wl_container_of(listener, ky_output, on);

    struct ky_output_client *client;
    wl_list_for_each(client, &ky_output->clients, link) {
        kywc_output_v1_send_enabled(client->resource, true);
        send_current_mode_to_output_client(client);
    }

    manager_update_idle_source(ky_output->manager);
}

static void handle_output_off(struct wl_listener *listener, void *data)
{
    struct ky_output *ky_output = wl_container_of(listener, ky_output, off);

    struct ky_output_client *client;
    wl_list_for_each(client, &ky_output->clients, link) {
        kywc_output_v1_send_enabled(client->resource, false);
    }

    manager_update_idle_source(ky_output->manager);
}

static void handle_output_scale(struct wl_listener *listener, void *data)
{
    struct ky_output *ky_output = wl_container_of(listener, ky_output, scale);

    struct ky_output_client *client;
    wl_list_for_each(client, &ky_output->clients, link) {
        kywc_output_v1_send_scale(client->resource,
                                  wl_fixed_from_double(ky_output->kywc_output->state.scale));
    }

    manager_update_idle_source(ky_output->manager);
}

static void handle_output_mode(struct wl_listener *listener, void *data)
{
    struct ky_output *ky_output = wl_container_of(listener, ky_output, mode);

    struct ky_output_client *client;
    wl_list_for_each(client, &ky_output->clients, link) {
        send_current_mode_to_output_client(client);
    }

    manager_update_idle_source(ky_output->manager);
}

static void handle_output_position(struct wl_listener *listener, void *data)
{
    struct ky_output *ky_output = wl_container_of(listener, ky_output, position);

    struct ky_output_client *client;
    wl_list_for_each(client, &ky_output->clients, link) {
        kywc_output_v1_send_position(client->resource, ky_output->kywc_output->state.lx,
                                     ky_output->kywc_output->state.ly);
    }

    manager_update_idle_source(ky_output->manager);
}

static void handle_output_transform(struct wl_listener *listener, void *data)
{
    struct ky_output *ky_output = wl_container_of(listener, ky_output, transform);

    struct ky_output_client *client;
    wl_list_for_each(client, &ky_output->clients, link) {
        kywc_output_v1_send_transform(client->resource, ky_output->kywc_output->state.transform);
    }

    manager_update_idle_source(ky_output->manager);
}

static void handle_output_power(struct wl_listener *listener, void *data)
{
    struct ky_output *ky_output = wl_container_of(listener, ky_output, power);

    struct ky_output_client *client;
    wl_list_for_each(client, &ky_output->clients, link) {
        kywc_output_v1_send_power(client->resource, ky_output->kywc_output->state.power);
    }

    manager_update_idle_source(ky_output->manager);
}

static void handle_output_brightness(struct wl_listener *listener, void *data)
{
    struct ky_output *ky_output = wl_container_of(listener, ky_output, brightness);

    struct ky_output_client *client;
    wl_list_for_each(client, &ky_output->clients, link) {
        kywc_output_v1_send_brightness(client->resource, ky_output->kywc_output->state.brightness);
    }

    manager_update_idle_source(ky_output->manager);
}

static void handle_output_color_temp(struct wl_listener *listener, void *data)
{
    struct ky_output *ky_output = wl_container_of(listener, ky_output, color_temp);

    struct ky_output_client *client;
    wl_list_for_each(client, &ky_output->clients, link) {
        kywc_output_v1_send_color_temp(client->resource, ky_output->kywc_output->state.color_temp);
    }

    manager_update_idle_source(ky_output->manager);
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct ky_output *ky_output = wl_container_of(listener, ky_output, destroy);

    struct ky_output_client *client, *tmp;
    wl_list_for_each_safe(client, tmp, &ky_output->clients, link) {
        struct wl_resource *resource, *tmp;
        wl_resource_for_each_safe(resource, tmp, &client->mode_resources) {
            kywc_output_mode_v1_send_finished(resource);
            wl_list_remove(wl_resource_get_link(resource));
            wl_list_init(wl_resource_get_link(resource));
            wl_resource_set_user_data(resource, NULL);
        }
        kywc_output_v1_send_finished(client->resource);
        wl_resource_set_user_data(client->resource, NULL);
        wl_list_remove(&client->link);
        free(client);
    }

    wl_list_remove(&ky_output->link);
    wl_list_remove(&ky_output->on.link);
    wl_list_remove(&ky_output->off.link);
    wl_list_remove(&ky_output->mode.link);
    wl_list_remove(&ky_output->scale.link);
    wl_list_remove(&ky_output->transform.link);
    wl_list_remove(&ky_output->position.link);
    wl_list_remove(&ky_output->brightness.link);
    wl_list_remove(&ky_output->color_temp.link);
    wl_list_remove(&ky_output->destroy.link);

    free(ky_output);
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct kywc_output *kywc_output = data;
    if (kywc_output->prop.is_virtual) {
        return;
    }

    struct ky_output *ky_output = calloc(1, sizeof(*ky_output));
    if (!ky_output) {
        return;
    }

    struct ky_output_manager *manager = wl_container_of(listener, manager, new_output);
    ky_output->manager = manager;
    wl_list_init(&ky_output->clients);
    wl_list_insert(&manager->outputs, &ky_output->link);

    ky_output->kywc_output = kywc_output;
    ky_output->on.notify = handle_output_on;
    wl_signal_add(&kywc_output->events.on, &ky_output->on);
    ky_output->off.notify = handle_output_off;
    wl_signal_add(&kywc_output->events.off, &ky_output->off);
    ky_output->scale.notify = handle_output_scale;
    wl_signal_add(&kywc_output->events.scale, &ky_output->scale);
    ky_output->mode.notify = handle_output_mode;
    wl_signal_add(&kywc_output->events.mode, &ky_output->mode);
    ky_output->position.notify = handle_output_position;
    wl_signal_add(&kywc_output->events.position, &ky_output->position);
    ky_output->transform.notify = handle_output_transform;
    wl_signal_add(&kywc_output->events.transform, &ky_output->transform);
    ky_output->power.notify = handle_output_power;
    wl_signal_add(&kywc_output->events.power, &ky_output->power);
    ky_output->brightness.notify = handle_output_brightness;
    wl_signal_add(&kywc_output->events.brightness, &ky_output->brightness);
    ky_output->color_temp.notify = handle_output_color_temp;
    wl_signal_add(&kywc_output->events.color_temp, &ky_output->color_temp);
    ky_output->destroy.notify = handle_output_destroy;
    wl_signal_add(&kywc_output->events.destroy, &ky_output->destroy);

    if (kywc_output == kywc_output_get_primary()) {
        manager->primary = ky_output;
    }

    /* send output and details */
    struct wl_resource *resource;
    struct ky_output_client *ky_client;
    wl_resource_for_each(resource, &manager->resources) {
        ky_client = create_output_client_for_resource(ky_output, resource);
        send_details_to_output_client(ky_client);
        kywc_output_manager_v1_send_done(resource);
    }
}

static void handle_primary_output(struct wl_listener *listener, void *data)
{
    struct ky_output_manager *manager = wl_container_of(listener, manager, primary_output);
    struct kywc_output *kywc_output = data;
    manager->primary = ky_output_from_kywc_output(manager, kywc_output);
    if (!manager->primary) {
        return;
    }

    struct wl_resource *resource;
    struct ky_output_client *client;
    wl_resource_for_each(resource, &manager->resources) {
        client = output_client_find_for_client(manager->primary, wl_resource_get_client(resource));
        kywc_output_manager_v1_send_primary(resource, client->resource);
        kywc_output_manager_v1_send_done(resource);
    }
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct ky_output_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct ky_output_manager *manager = wl_container_of(listener, manager, display_destroy);
    wl_list_remove(&manager->display_destroy.link);
    wl_list_remove(&manager->new_output.link);
    wl_list_remove(&manager->primary_output.link);
    if (manager->idle_source) {
        wl_event_source_remove(manager->idle_source);
    }
    wl_global_destroy(manager->global);
}

bool ky_output_manager_create(struct server *server)
{
    struct ky_output_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    manager->global = wl_global_create(server->display, &kywc_output_manager_v1_interface, 1,
                                       manager, ky_output_manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_WARN, "kywc output manager create failed");
        free(manager);
        return false;
    }

    wl_list_init(&manager->resources);
    wl_list_init(&manager->outputs);

    manager->event_loop = wl_display_get_event_loop(server->display);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    manager->new_output.notify = handle_new_output;
    kywc_output_add_new_listener(&manager->new_output);
    manager->primary_output.notify = handle_primary_output;
    kywc_output_add_primary_listener(&manager->primary_output);

    return true;
}
