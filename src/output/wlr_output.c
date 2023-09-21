// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>

#include "output_p.h"

struct wlr_output_management {
    struct wlr_output_manager_v1 *output_manager;
    struct wl_listener output_apply;

    struct wlr_output_power_manager_v1 *power_manager;
    struct wl_listener power_set_mode;

    struct wl_list heads;

    struct wl_listener new_output;
    struct wl_listener configured;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct output_head {
    struct kywc_output *kywc_output;
    struct wl_list link;
    struct wl_listener destroy;
};

static struct wlr_output_management *management = NULL;

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&management->server_destroy.link);

    free(management);
    management = NULL;
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&management->display_destroy.link);
    wl_list_remove(&management->new_output.link);
    wl_list_remove(&management->configured.link);
}

static void manager_update_configuration(void)
{
    struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();

    struct output_head *head;
    wl_list_for_each(head, &management->heads, link) {
        struct wlr_output *wlr_output = output_from_kywc_output(head->kywc_output)->wlr_output;
        if (head->kywc_output->destroying) {
            continue;
        }
        struct wlr_output_configuration_head_v1 *head_v1 =
            wlr_output_configuration_head_v1_create(config, wlr_output);
        head_v1->state.enabled = head->kywc_output->state.enabled;
        head_v1->state.x = head->kywc_output->state.lx;
        head_v1->state.y = head->kywc_output->state.ly;
    }
    wlr_output_manager_v1_set_configuration(management->output_manager, config);
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct output_head *head = wl_container_of(listener, head, destroy);

    wl_list_remove(&head->destroy.link);
    wl_list_remove(&head->link);

    free(head);
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct output_head *head = calloc(1, sizeof(struct output_head));
    if (!head) {
        return;
    }

    struct kywc_output *kywc_output = data;
    head->kywc_output = kywc_output;
    wl_list_insert(&management->heads, &head->link);

    head->destroy.notify = handle_output_destroy;
    wl_signal_add(&kywc_output->events.destroy, &head->destroy);

    manager_update_configuration();
}

static void handle_configured(struct wl_listener *listener, void *data)
{
    manager_update_configuration();
}

static void handle_output_apply(struct wl_listener *listener, void *data)
{
    kywc_log(KYWC_DEBUG, "wlr output manager apply");
    struct wlr_output_configuration_v1 *config = data;
    struct kywc_output *primary_output = output_manager_get_primary();

    if (wl_list_empty(&management->heads) || !primary_output) {
        kywc_log(KYWC_WARN, "configuration cannot be applied");
        wlr_output_configuration_v1_send_failed(config);
        wlr_output_configuration_v1_destroy(config);
        return;
    }

    struct wlr_output_configuration_head_v1 *head_v1;
    wl_list_for_each(head_v1, &config->heads, link) {
        struct kywc_output_state pending;
        pending.enabled = pending.power = head_v1->state.enabled;
        if (head_v1->state.mode) {
            pending.width = head_v1->state.mode->width;
            pending.height = head_v1->state.mode->height;
            pending.refresh = head_v1->state.mode->refresh;
        } else {
            pending.width = head_v1->state.custom_mode.width;
            pending.height = head_v1->state.custom_mode.height;
            pending.refresh = head_v1->state.custom_mode.refresh;
        }
        pending.scale = head_v1->state.scale;
        pending.transform = head_v1->state.transform;
        pending.lx = head_v1->state.x;
        pending.ly = head_v1->state.y;
        pending.vrr_policy = head_v1->state.adaptive_sync_enabled;

        struct output *output = output_from_wlr_output(head_v1->state.output);
        output_manager_add_output_pending_state(output, pending);
    }

    kywc_output_set_pending_primary(primary_output);

    if (!output_manager_configure_outputs()) {
        wlr_output_configuration_v1_send_failed(config);
        wlr_output_configuration_v1_destroy(config);
        return;
    }
    wlr_output_configuration_v1_send_succeeded(config);
    wlr_output_configuration_v1_destroy(config);
}

static void handle_power_set_mode(struct wl_listener *listener, void *data)
{
    struct wlr_output_power_v1_set_mode_event *event = data;
    struct output *output = output_from_wlr_output(event->output);

    struct kywc_output_state state = output->base.state;
    state.power = event->mode == ZWLR_OUTPUT_POWER_V1_MODE_OFF ? false : true;

    kywc_output_set_state(&output->base, &state);
}

bool wlr_output_management_create(struct server *server)
{
    management = calloc(1, sizeof(struct wlr_output_management));
    if (!management) {
        return false;
    }

    wl_list_init(&management->heads);

    management->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &management->server_destroy);
    management->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &management->display_destroy);

    management->new_output.notify = handle_new_output;
    kywc_output_add_new_listener(&management->new_output);
    management->configured.notify = handle_configured;
    output_manager_add_configured_listener(&management->configured);

    management->output_manager = wlr_output_manager_v1_create(server->display);
    management->output_apply.notify = handle_output_apply;
    wl_signal_add(&management->output_manager->events.apply, &management->output_apply);

    management->power_manager = wlr_output_power_manager_v1_create(server->display);
    management->power_set_mode.notify = handle_power_set_mode;
    wl_signal_add(&management->power_manager->events.set_mode, &management->power_set_mode);

    return true;
}
