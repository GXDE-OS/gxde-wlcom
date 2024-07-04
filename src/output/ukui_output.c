// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include "ukui-output-v1-protocol.h"

#include "output_p.h"
#include "server.h"

#define UKUI_OUTPUT_VERSION 1

struct ukui_output_management {
    struct wl_global *global;
    struct wl_event_loop *event_loop;
    struct wl_event_source *idle_source;

    struct wl_list resources;
    struct wl_list outputs;

    struct wl_listener new_enabled_output;
    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct ukui_output {
    struct ukui_output_management *management;
    struct wl_list link;

    struct wl_list clients; // ukui_output_client.link

    struct kywc_output *kywc_output;

    struct wl_listener disable;
    struct wl_listener usable_area_changed;
};

struct ukui_output_client {
    struct wl_resource *resource;
    struct wl_list link;

    struct ukui_output *ukui_output;
};

static void send_output_initial_state(struct ukui_output_client *ukui_client)
{
    ukui_output_v1_send_name(ukui_client->resource, ukui_client->ukui_output->kywc_output->name);

    struct output *output = output_from_kywc_output(ukui_client->ukui_output->kywc_output);
    ukui_output_v1_send_usable_area(ukui_client->resource, output->usable_area.x,
                                    output->usable_area.y, output->usable_area.width,
                                    output->usable_area.height);
    kywc_log(KYWC_DEBUG, "ukui output usable area: %s, %d, %d, %d x %d", output->base.name,
             output->usable_area.x, output->usable_area.y, output->usable_area.width,
             output->usable_area.height);
}

static void management_idle_send_done(void *data)
{
    struct ukui_output_management *management = data;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &management->resources) {
        ukui_output_management_v1_send_done(resource);
    }

    management->idle_source = NULL;
}

static void management_update_idle_source(struct ukui_output_management *management)
{
    if (management->idle_source || wl_list_empty(&management->resources)) {
        return;
    }

    management->idle_source =
        wl_event_loop_add_idle(management->event_loop, management_idle_send_done, management);
}

static void handle_ukui_output_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void ukui_output_resource_destroy(struct wl_resource *resource)
{
    struct ukui_output_client *ukui_client = wl_resource_get_user_data(resource);
    if (!ukui_client) {
        return;
    }

    wl_list_remove(&ukui_client->link);
    free(ukui_client);
}

static const struct ukui_output_v1_interface ukui_output_impl = {
    .destroy = handle_ukui_output_destroy,
};

static struct ukui_output_client *
create_client_for_resource(struct ukui_output *ukui_output, struct wl_resource *management_resource)
{
    struct ukui_output_client *ukui_client = calloc(1, sizeof(*ukui_client));
    if (!ukui_client) {
        return NULL;
    }

    struct wl_client *client = wl_resource_get_client(management_resource);
    struct wl_resource *resource = wl_resource_create(
        client, &ukui_output_v1_interface, wl_resource_get_version(management_resource), 0);
    if (!resource) {
        wl_client_post_no_memory(client);
        free(ukui_client);
        return NULL;
    }

    ukui_client->resource = resource;
    ukui_client->ukui_output = ukui_output;
    wl_list_insert(&ukui_output->clients, &ukui_client->link);

    wl_resource_set_implementation(resource, &ukui_output_impl, ukui_client,
                                   ukui_output_resource_destroy);

    return ukui_client;
}

static void handle_output_disable(struct wl_listener *listener, void *data)
{
    struct ukui_output *ukui_output = wl_container_of(listener, ukui_output, disable);
    struct ukui_output_client *ukui_client, *tmp;
    wl_list_for_each_safe(ukui_client, tmp, &ukui_output->clients, link) {
        ukui_output_v1_send_finished(ukui_client->resource);
        wl_resource_set_user_data(ukui_client->resource, NULL);
        wl_list_remove(&ukui_client->link);
        free(ukui_client);
    }

    wl_list_remove(&ukui_output->link);
    wl_list_remove(&ukui_output->usable_area_changed.link);
    wl_list_remove(&ukui_output->disable.link);
    free(ukui_output);
}

static void handle_usable_area_changed(struct wl_listener *listener, void *data)
{
    struct ukui_output *ukui_output = wl_container_of(listener, ukui_output, usable_area_changed);
    struct output *output = output_from_kywc_output(ukui_output->kywc_output);

    struct ukui_output_client *ukui_client;
    wl_list_for_each(ukui_client, &ukui_output->clients, link) {
        ukui_output_v1_send_usable_area(ukui_client->resource, output->usable_area.x,
                                        output->usable_area.y, output->usable_area.width,
                                        output->usable_area.height);
    }
    management_update_idle_source(ukui_output->management);
}

static void handle_new_enabled_output(struct wl_listener *listener, void *data)
{
    struct kywc_output *kywc_output = data;
    struct output *output = output_from_kywc_output(kywc_output);
    struct ukui_output_management *management =
        wl_container_of(listener, management, new_enabled_output);

    struct ukui_output *ukui_output = calloc(1, sizeof(struct ukui_output));
    if (!ukui_output) {
        return;
    }

    ukui_output->management = management;
    wl_list_init(&ukui_output->clients);
    wl_list_insert(&management->outputs, &ukui_output->link);

    ukui_output->disable.notify = handle_output_disable;
    wl_signal_add(&output->events.disable, &ukui_output->disable);
    ukui_output->usable_area_changed.notify = handle_usable_area_changed;
    wl_signal_add(&output->events.usable_area, &ukui_output->usable_area_changed);

    /* send output usable_area */
    struct wl_resource *resource;
    struct ukui_output_client *ukui_client;
    wl_resource_for_each(resource, &management->resources) {
        ukui_client = create_client_for_resource(ukui_output, resource);
        if (ukui_client) {
            send_output_initial_state(ukui_client);
        }
        ukui_output_management_v1_send_done(resource);
    }
}

static struct ukui_output_client *ukui_output_client_from_client(struct ukui_output *ukui_output,
                                                                 struct wl_client *client)
{
    struct ukui_output_client *ukui_client;
    wl_list_for_each(ukui_client, &ukui_output->clients, link) {
        if (client == wl_resource_get_client(ukui_client->resource)) {
            return ukui_client;
        }
    }
    return NULL;
}

static void handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void ukui_output_management_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static const struct ukui_output_management_v1_interface ukui_output_management_impl = {
    .destroy = handle_destroy,
};

static void ukui_output_management_bind(struct wl_client *client, void *data, uint32_t version,
                                        uint32_t id)
{
    struct ukui_output_management *management = data;
    struct wl_resource *resource =
        wl_resource_create(client, &ukui_output_management_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &ukui_output_management_impl, management,
                                   ukui_output_management_resource_destroy);
    wl_list_insert(&management->resources, wl_resource_get_link(resource));

    struct ukui_output *ukui_output;
    wl_list_for_each(ukui_output, &management->outputs, link) {
        create_client_for_resource(ukui_output, resource);
    }

    struct ukui_output_client *ukui_client;
    wl_list_for_each_reverse(ukui_output, &management->outputs, link) {
        ukui_client = ukui_output_client_from_client(ukui_output, client);
        if (ukui_client) {
            send_output_initial_state(ukui_client);
        }
    }
    ukui_output_management_v1_send_done(resource);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct ukui_output_management *management =
        wl_container_of(listener, management, display_destroy);
    wl_list_remove(&management->display_destroy.link);
    wl_list_remove(&management->new_enabled_output.link);

    if (management->idle_source) {
        wl_event_source_remove(management->idle_source);
    }

    wl_global_destroy(management->global);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct ukui_output_management *management =
        wl_container_of(listener, management, server_destroy);
    wl_list_remove(&management->server_destroy.link);
    free(management);
}

bool ukui_output_management_create(struct server *server)
{
    struct ukui_output_management *management = calloc(1, sizeof(struct ukui_output_management));
    if (!management) {
        return false;
    }

    management->global =
        wl_global_create(server->display, &ukui_output_management_v1_interface, UKUI_OUTPUT_VERSION,
                         management, ukui_output_management_bind);
    if (!management->global) {
        kywc_log(KYWC_WARN, "ukui output management create failed");
        free(management);
        return false;
    }

    wl_list_init(&management->resources);
    wl_list_init(&management->outputs);

    management->event_loop = wl_display_get_event_loop(server->display);

    management->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &management->server_destroy);
    management->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &management->display_destroy);

    management->new_enabled_output.notify = handle_new_enabled_output;
    output_manager_add_new_enabled_listener(&management->new_enabled_output);

    return true;
}
