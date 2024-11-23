// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include "config.h"
#include "output_p.h"

struct output_uuid {
    const char *name;
    const char *uuid;
};

static void output_get_layout(struct output *output, const char *active_layout, char *layout)
{
    strcpy(layout, active_layout);
    layout[UUID_SIZE - 1] = ':';
    strcpy(layout + UUID_SIZE, output->base.uuid);
}

static const char *output_manager_get_active_layout(struct output_manager *output_manager)
{
    struct output_manager *om = output_manager;
    if (!om->layout_config || !om->layout_config->json) {
        return NULL;
    }

    /* get outputs_layout from layouts */
    json_object *outputs_layout =
        json_object_object_get(om->layout_config->json, om->outputs_layout);
    if (!outputs_layout) {
        return NULL;
    }

    json_object *data;
    /* get active_layout from outputs_layout */
    if (json_object_object_get_ex(outputs_layout, "active_layout", &data)) {
        return json_object_get_string(data);
    }

    return NULL;
}

static void output_manager_set_active_layout(struct output_manager *manager,
                                             const char *active_layout)
{
    if (!manager->layout_config || !manager->layout_config->json || !active_layout) {
        return;
    }

    /* get outputs_layout in layouts, create if no */
    json_object *outputs_layout =
        json_object_object_get(manager->layout_config->json, manager->outputs_layout);
    if (!outputs_layout) {
        outputs_layout = json_object_new_object();
        json_object_object_add(manager->layout_config->json, manager->outputs_layout,
                               outputs_layout);
    }

    json_object_object_add(outputs_layout, "active_layout", json_object_new_string(active_layout));
}

static bool output_read_layout_config(struct output *output, struct kywc_output_state *state,
                                      const char *active_layout)
{
    struct output_manager *om = output->manager;
    if (!om->layout_config || !om->layout_config->json || !active_layout) {
        return false;
    }

    char layout[UUID_SIZE * 2];
    output_get_layout(output, active_layout, layout);
    json_object *config = json_object_object_get(om->layout_config->json, layout);
    if (!config) {
        return false;
    }

    json_object *data;

    if (json_object_object_get_ex(config, "enabled", &data)) {
        state->enabled = state->power = json_object_get_boolean(data);
    }
    if (!state->enabled) {
        return true;
    }

    if (json_object_object_get_ex(config, "primary", &data)) {
        if (json_object_get_boolean(data)) {
            output_set_pending_primary(output);
        }
    }
    if (json_object_object_get_ex(config, "width", &data)) {
        state->width = json_object_get_int(data);
    }
    if (json_object_object_get_ex(config, "height", &data)) {
        state->height = json_object_get_int(data);
    }
    if (json_object_object_get_ex(config, "refresh", &data)) {
        state->refresh = json_object_get_int(data);
    }
    if (json_object_object_get_ex(config, "transform", &data)) {
        state->transform = json_object_get_int(data);
    }
    if (json_object_object_get_ex(config, "scale", &data)) {
        state->scale = json_object_get_double(data);
    }
    if (json_object_object_get_ex(config, "lx", &data)) {
        state->lx = json_object_get_int(data);
    }
    if (json_object_object_get_ex(config, "ly", &data)) {
        state->ly = json_object_get_int(data);
    }
    return true;
}

static void output_write_layout_config(struct output *output, const char *active_layout)
{
    struct output_manager *om = output->manager;
    if (!om->layout_config || !om->layout_config->json || !active_layout) {
        return;
    }

    char layout[UUID_SIZE * 2];
    output_get_layout(output, active_layout, layout);

    json_object *config = json_object_object_get(om->layout_config->json, layout);
    if (!config) {
        config = json_object_new_object();
        json_object_object_add(om->layout_config->json, layout, config);
    }

    struct kywc_output *kywc_output = &output->base;
    struct kywc_output_state *state = &kywc_output->state;
    bool primary = kywc_output_get_primary() == kywc_output;

    json_object_object_add(config, "enabled", json_object_new_boolean(state->enabled));
    if (!state->enabled) {
        return;
    }

    json_object_object_add(config, "primary", json_object_new_boolean(primary));
    json_object_object_add(config, "width", json_object_new_int(state->width));
    json_object_object_add(config, "height", json_object_new_int(state->height));
    json_object_object_add(config, "refresh", json_object_new_int(state->refresh));
    json_object_object_add(config, "scale", json_object_new_double(state->scale));
    json_object_object_add(config, "transform", json_object_new_int(state->transform));
    json_object_object_add(config, "lx", json_object_new_int(state->lx));
    json_object_object_add(config, "ly", json_object_new_int(state->ly));
}

static int compare_output_uuid(const void *p1, const void *p2)
{
    const char *v1 = ((struct output_uuid *)p1)->name;
    const char *v2 = ((struct output_uuid *)p2)->name;
    return strcmp(v1, v2);
}

static void output_manager_generate_layout(struct output_manager *output_manager, char *layout_uuid,
                                           bool is_active_layout)
{
    struct output_uuid *o_uuids = NULL;
    int actual_cnt = 0;

    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        struct kywc_output *kywc_output = &output->base;
        if (kywc_output == output_manager_get_fallback()) {
            continue;
        }
        if (is_active_layout && !kywc_output->state.enabled) {
            continue;
        }

        o_uuids = realloc(o_uuids, (actual_cnt + 1) * sizeof(struct output_uuid));
        o_uuids[actual_cnt].name = kywc_output->name;
        o_uuids[actual_cnt].uuid = kywc_output->uuid;
        actual_cnt++;
    }

    if (!actual_cnt) {
        return;
    }

    if (actual_cnt == 1) {
        strcpy(layout_uuid, o_uuids[0].uuid);
        free(o_uuids);
        return;
    }

    qsort(o_uuids, actual_cnt, sizeof(struct output_uuid), compare_output_uuid);

    uint8_t *uuids = malloc(actual_cnt * (UUID_SIZE - 1));
    for (int i = 0; i < actual_cnt; ++i) {
        memcpy(uuids + i * (UUID_SIZE - 1), o_uuids[i].uuid, (UUID_SIZE - 1));
    }
    free(o_uuids);

    const char *uuid = kywc_identifier_md5_generate_uuid(uuids, actual_cnt * (UUID_SIZE - 1));
    strcpy(layout_uuid, uuid);
    free((void *)uuid);

    free(uuids);
}

void output_manager_get_layout_configs(struct output_manager *output_manager)
{
    if (!output_manager->has_layout_manager || output_manager->server->terminate) {
        return;
    }

    if (!output_manager_has_actual_outputs()) {
        return;
    }
    /* update current outputs layout */
    output_manager_generate_layout(output_manager, output_manager->outputs_layout, false);

    const char *active_layout = output_manager_get_active_layout(output_manager);
    kywc_log(KYWC_INFO, "configure outputs layout %s with active layout %s",
             output_manager->outputs_layout, active_layout);

    /* get all outputs configuration */
    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        struct kywc_output *kywc_output = &output->base;
        if (kywc_output == output_manager_get_fallback()) {
            continue;
        }

        struct kywc_output_state pending = { 0 };
        if (output_read_layout_config(output, &pending, active_layout)) {
            output_manager_add_output_pending_state(output, &pending);
            continue;
        }

        struct kywc_output_mode *mode = kywc_output_preferred_mode(kywc_output);
        pending.enabled = pending.power = true;
        pending.lx = pending.ly = -1;
        pending.width = mode->width;
        pending.height = mode->height;
        pending.refresh = mode->refresh;
        pending.scale = kywc_output_preferred_scale(kywc_output, pending.width, pending.height);
        output_manager_add_output_pending_state(output, &pending);
    }
}

static void output_manager_save_layouts(struct output_manager *manager)
{
    char active_layout[UUID_SIZE];
    output_manager_generate_layout(manager, active_layout, true);
    output_manager_set_active_layout(manager, active_layout);

    struct output *output;
    wl_list_for_each(output, &manager->outputs, link) {
        struct kywc_output *kywc_output = &output->base;
        if (kywc_output == output_manager_get_fallback()) {
            continue;
        }
        output_write_layout_config(output, active_layout);
    }

    if (kywc_log_get_level() >= KYWC_INFO) {
        kywc_log(KYWC_INFO, "save outputs layout %s with active layout %s", manager->outputs_layout,
                 active_layout);

        wl_list_for_each(output, &manager->outputs, link) {
            struct kywc_output *kywc_output = &output->base;
            if (kywc_output == output_manager_get_fallback()) {
                continue;
            }
            struct kywc_output_state *state = &kywc_output->state;
            bool primary = kywc_output_get_primary() == kywc_output;
            kywc_log(KYWC_INFO,
                     "\t output %s: mode (%d x %d @ %d) scale %f pos (%d, %d) transform %d %s %s",
                     kywc_output->name, state->width, state->height, state->refresh, state->scale,
                     state->lx, state->ly, state->transform,
                     state->enabled ? "enabled" : "disabled", primary ? "primary" : "");
        }
    }
}

static void output_manager_handle_configured(struct wl_listener *listener, void *data)
{
    struct output_manager *output_manager = wl_container_of(listener, output_manager, configured);

    if (!output_manager->has_layout_manager || !output_manager_has_actual_outputs()) {
        return;
    }

    output_manager_save_layouts(output_manager);
}

bool output_manager_layout_init(struct output_manager *output_manager)
{
    char *env = getenv("KYWC_USE_LAYOUT_MANAGER");
    bool use_layout = env && strcmp(env, "1") == 0;
    if (!use_layout) {
        return false;
    }

    output_manager->layout_config =
        config_manager_add_config("layouts", NULL, NULL, NULL, NULL, output_manager);
    /* listener output configured signal */
    output_manager->configured.notify = output_manager_handle_configured;
    output_manager_add_configured_listener(&output_manager->configured);
    return !!output_manager->layout_config;
}
