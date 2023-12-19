// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <xf86drm.h>

#include <wlr/backend/drm.h>
#include <wlr/backend/headless.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output_layout.h>

#include <kywc/log.h>

#include "output_p.h"
#include "xwayland.h"

static struct output_manager *output_manager = NULL;
static char *unknown = "unknown";

struct output *output_from_kywc_output(struct kywc_output *kywc_output)
{
    struct output *output = wl_container_of(kywc_output, output, base);
    return output;
}

struct output *output_from_wlr_output(struct wlr_output *wlr_output)
{
    return wlr_output->data;
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&output_manager->server_destroy.link);
    wl_list_remove(&output_manager->server_suspend.link);
    wl_list_remove(&output_manager->server_resume.link);

    free(output_manager);
    output_manager = NULL;
}

static void handle_server_suspend(struct wl_listener *listener, void *data)
{
    kywc_log(KYWC_DEBUG, "handle server D-Bus suspend");
    output_manager_power_outputs(false);
}

static void handle_server_resume(struct wl_listener *listener, void *data)
{
    kywc_log(KYWC_DEBUG, "handle server D-Bus resume");
    output_manager_power_outputs(true);
}

static void output_add_mode(struct kywc_output_prop *prop, struct wlr_output_mode *mode)
{
    struct kywc_output_mode *new = calloc(1, sizeof(struct kywc_output_mode));
    if (!new) {
        return;
    }

    new->width = mode->width;
    new->height = mode->height;
    new->refresh = mode->refresh;
    new->preferred = mode->preferred;
    wl_list_insert(&prop->modes, &new->link);
}

static void output_get_prop(struct output *output, struct kywc_output_prop *prop)
{
    struct wlr_output *wlr_output = output->wlr_output;

    prop->capability = 0;
    prop->phys_width = wlr_output->phys_width;
    prop->phys_height = wlr_output->phys_height;
    prop->make = wlr_output->make;
    prop->model = wlr_output->model;
    prop->serial = wlr_output->serial;
    prop->desc = wlr_output->description;
    prop->is_virtual = wlr_output_is_headless(wlr_output);
    prop->brightness_support = output_support_brightness(output);
    prop->gamma_size = wlr_output_get_gamma_size(wlr_output);

    /* fix zero mode in some backend, like wayland */
    if (wl_list_empty(&wlr_output->modes)) {
        struct wlr_output_mode mode = {
            .width = wlr_output->width,
            .height = wlr_output->height,
            .refresh = wlr_output->refresh,
            .preferred = true,
        };
        output_add_mode(prop, &mode);
    } else {
        struct wlr_output_mode *mode;
        wl_list_for_each(mode, &wlr_output->modes, link) {
            output_add_mode(prop, mode);
        }
    }
}

static void output_get_state(struct output *output, struct kywc_output_state *state)
{
    struct wlr_output *wlr_output = output->wlr_output;

    // FIXME: enabled and power state
    state->enabled = wlr_output->enabled;
    state->power = wlr_output->enabled;
    state->width = wlr_output->width;
    state->height = wlr_output->height;
    state->refresh = wlr_output->refresh;
    state->transform = wlr_output->transform;
    state->scale = wlr_output->scale;

    // state->vrr_policy = wlr_output->adaptive_sync_status;
    struct wlr_output_layout_output *layout_output;
    layout_output = wlr_output_layout_get(output_manager->server->layout, wlr_output);
    if (layout_output) {
        state->lx = layout_output->x;
        state->ly = layout_output->y;
    } else {
        state->lx = state->ly = -1;
    }

    struct kywc_output *kywc_output = &output->base;
    if (kywc_output == output_manager->fallback_output) {
        state->lx = 0;
        state->ly = 0;
    }
    if (!output_get_brightness(kywc_output, &state->brightness)) {
        state->brightness = 80;
    }
    state->color_temp = 6500;
}

static void fallback_output_set_state(struct kywc_output *kywc_output, bool enabled)
{
    struct kywc_output_state state = kywc_output->state;

    if (!state.enabled && enabled) {
        state.power = true;
        state.scale = 1.0;

        struct kywc_output_mode *mode = kywc_output_preferred_mode(kywc_output);
        state.width = mode->width;
        state.height = mode->height;
        state.refresh = mode->refresh;
    }
    state.enabled = enabled;

    kywc_output_set_state(kywc_output, &state);
}

static void output_lock_software_cursors(struct output *output)
{
    if (!wlr_output_is_drm(output->wlr_output)) {
        return;
    }

    int drm_fd = wlr_backend_get_drm_fd(output->wlr_output->backend);
    drmVersion *version = drmGetVersion(drm_fd);
    /* use software cursor, linear buffer is external only in nvidia driver */
    if (version && strcmp(version->name, "nvidia-drm") == 0) {
        wlr_output_lock_software_cursors(output->wlr_output, true);
    }
    drmFreeVersion(version);
}

static struct output *output_create(const char *name, struct wlr_output *wlr_output)
{
    struct output *output = calloc(1, sizeof(struct output));
    if (!output) {
        return NULL;
    }

    output->wlr_output = wlr_output;
    wlr_output->data = output;

    struct kywc_output *kywc_output = &output->base;
    kywc_output->name = name;

    wl_signal_init(&kywc_output->events.on);
    wl_signal_init(&kywc_output->events.off);
    wl_signal_init(&kywc_output->events.scale);
    wl_signal_init(&kywc_output->events.transform);
    wl_signal_init(&kywc_output->events.mode);
    wl_signal_init(&kywc_output->events.position);
    wl_signal_init(&kywc_output->events.power);
    wl_signal_init(&kywc_output->events.frame);
    wl_signal_init(&kywc_output->events.destroy);

    wl_signal_init(&output->events.geometry);
    wl_signal_init(&output->events.usable_area);
    wl_signal_init(&output->events.update_usable_area);
    wl_signal_init(&output->events.update_late_usable_area);

    output_add_common_modes(output);
    /* get props */
    wl_list_init(&kywc_output->prop.modes);
    output_get_prop(output, &kywc_output->prop);
    /* fill null pointer with unknown, but adapter should avoid null */
    if (!kywc_output->prop.model) {
        kywc_output->prop.model = unknown;
    }
    if (!kywc_output->prop.desc) {
        kywc_output->prop.desc = kywc_output->name;
    }
    if (!kywc_output->prop.make) {
        kywc_output->prop.make = unknown;
    }
    if (kywc_output->prop.is_virtual && strcmp(name, "FALLBACK") == 0) {
        output_manager->fallback_output = kywc_output;
    }

    output->manager = output_manager;
    if (kywc_output->prop.is_virtual) {
        wl_list_init(&output->link);
    } else {
        wl_list_insert(&output_manager->outputs, &output->link);
    }

    output_get_state(output, &kywc_output->state);

    /* read config and apply it */
    output->modeset = false;
    if (!kywc_output->prop.is_virtual) {
        output_uuid_generate(kywc_output);
        output_manager_get_layout_configs(output_manager);
        output_manager_configure_outputs();
    }

    if (kywc_output == output_manager->fallback_output && wl_list_empty(&output_manager->outputs)) {
        fallback_output_set_state(kywc_output, true);
        kywc_output_set_primary(kywc_output);
        output_manager_emit_configured();
    }
    output->modeset = true;

    output_lock_software_cursors(output);

    wl_signal_emit_mutable(&output_manager->events.new_output, kywc_output);

    return output;
}

static void handle_output_frame(struct wl_listener *listener, void *data)
{
    struct output *output = wl_container_of(listener, output, frame);
    struct kywc_output *kywc_output = &output->base;

    /* make sure something is done before commit */
    wl_signal_emit_mutable(&kywc_output->events.frame, NULL);

#if HAVE_WLR_SCENE | HAVE_KYCOM_SCENE
    ky_scene_output_commit(output->scene_output, NULL);

    struct timespec now = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &now);
    ky_scene_output_send_frame_done(output->scene_output, &now);
#else
    if (!wlr_output->needs_frame) {
        kywc_log(KYWC_DEBUG, "no frame needed, stop commit");
        return;
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    struct wlr_render_pass *pass = wlr_output_begin_render_pass(wlr_output, &state, NULL, NULL);
    wlr_render_pass_add_rect(
        pass, &(struct wlr_render_rect_options){
                  .box = { .width = wlr_output->width, .height = wlr_output->height },
                  .color = { 0.25f, 0.25f, 0.25f, 1 },
              });
    wlr_output_add_software_cursors_to_render_pass(wlr_output, pass, NULL);
    wlr_render_pass_submit(pass);
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);
#endif
}

static void handle_output_damage(struct wl_listener *listener, void *data)
{
    struct output *output = wl_container_of(listener, output, damage);

    wlr_output_schedule_frame(output->wlr_output);
}

static void handle_output_needs_frame(struct wl_listener *listener, void *data)
{
    struct output *output = wl_container_of(listener, output, needs_frame);

    wlr_output_schedule_frame(output->wlr_output);
}

static void output_destroy(struct output *output)
{
    struct kywc_output *kywc_output = &output->base;

    kywc_output->destroying = true;
    wl_signal_emit_mutable(&kywc_output->events.destroy, NULL);

    wl_list_remove(&output->link);
    /* get layouts configure outputs */
    output_manager_get_layout_configs(output_manager);
    output_manager_configure_outputs();

    if (output_manager->fallback_output && wl_list_empty(&output_manager->outputs)) {
        fallback_output_set_state(output_manager->fallback_output, true);
        kywc_output_set_primary(output_manager->fallback_output);
        output_manager_emit_configured();
    }

    struct kywc_output_mode *mode, *tmp_mode;
    wl_list_for_each_safe(mode, tmp_mode, &kywc_output->prop.modes, link) {
        wl_list_remove(&mode->link);
        free(mode);
    }

    free(output);
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct output *output = wl_container_of(listener, output, destroy);

    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->damage.link);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->needs_frame.link);

    /* output may be disabled before */
    if (output->scene_output) {
        ky_scene_output_destroy(output->scene_output);
        wlr_output_layout_remove(output_manager->server->layout, output->wlr_output);
    }

    output_destroy(output);
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct server *server = output_manager->server;
    struct wlr_output *wlr_output = data;

    if (!wlr_output_init_render(wlr_output, server->allocator, server->renderer)) {
        kywc_log(KYWC_ERROR, "unable to init output renderer");
        return;
    }

    struct output *output = output_create(wlr_output->name, wlr_output);
    if (!output) {
        return;
    }

    output->frame.notify = handle_output_frame;
    output->destroy.notify = handle_output_destroy;
    output->damage.notify = handle_output_damage;
    output->needs_frame.notify = handle_output_needs_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
#if HAVE_WLR_SCENE | HAVE_KYCOM_SCENE
    wl_list_init(&output->damage.link);
    wl_list_init(&output->needs_frame.link);
#else
    wl_signal_add(&wlr_output->events.damage, &output->damage);
    wl_signal_add(&wlr_output->events.needs_frame, &output->needs_frame);
#endif
}

void output_manager_power_outputs(bool power)
{
    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (!output->base.state.enabled || output->base.state.power == power) {
            continue;
        }
        struct kywc_output_state state = output->base.state;
        state.power = power;
        kywc_output_set_state(&output->base, &state);
    }
}

float output_manager_get_scale(void)
{
    float scale = 1.0;
    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (!output->base.state.enabled) {
            continue;
        }
        if (output->base.state.scale > scale) {
            scale = output->base.state.scale;
        }
    }
    return scale;
}

void output_manager_update_scale(float scale)
{
    /* update xdg_output pos and size */
    xdg_output_update_scale(scale);

    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (!output->base.state.enabled) {
            continue;
        }

        struct wl_resource *resource;
        wl_resource_for_each(resource, &output->wlr_output->resources) {
            if (xwayland_check_client(wl_resource_get_client(resource))) {
                if (wl_resource_get_version(resource) >= WL_OUTPUT_DONE_SINCE_VERSION) {
                    wl_output_send_done(resource);
                }
                break;
            }
        }
    }
}

static struct output_pending_config *get_output_pending_config(struct output *output)
{
    struct output_pending_config *pending_config;
    wl_list_for_each(pending_config, &output_manager->output_configs, link) {
        if (pending_config->output == output) {
            return pending_config;
        }
    }
    return NULL;
}

bool output_manager_configure_outputs(void)
{
    bool ret = false;

    if (wl_list_empty(&output_manager->outputs)) {
        goto failed;
    }

    /* 1.check configs */
    bool need_fix_primary_output = false;
    if (!output_manager->pending_primary && output_manager->primary_output &&
        !output_manager->primary_output->prop.is_virtual) {
        struct output *output;
        wl_list_for_each(output, &output_manager->outputs, link) {
            struct kywc_output *kywc_output = &output->base;
            /* filter primary output is destroing output */
            if (kywc_output != output_manager->primary_output) {
                continue;
            }
            output_manager->pending_primary = output_manager->primary_output;
            kywc_log(KYWC_WARN, "pending_primay is null set as the primary_output:%s",
                     output_manager->primary_output->name);
            break;
        }
    }
    need_fix_primary_output = !output_manager->pending_primary;

    /* primary output may be disabled, fixup it */
    struct output_pending_config *pending_config, *temp;
    wl_list_for_each(pending_config, &output_manager->output_configs, link) {
        /* It's going to disable the primary output and no new primary config */
        struct kywc_output *kywc_output = &pending_config->output->base;
        if (kywc_output == output_manager->pending_primary && !pending_config->state.enabled) {
            need_fix_primary_output = true;
            break;
        }
    }

    /* if all outputs will be disabled in config, find others not in config */
    bool have_enabled_output = false;
    bool have_auto_coord = false;
    bool have_zero_coord = false;
    int auto_coord_outputs = 0;
    struct output *output = NULL;
    wl_list_for_each(output, &output_manager->outputs, link) {
        pending_config = get_output_pending_config(output);
        if (pending_config) {
            have_enabled_output |= pending_config->state.enabled;
            have_zero_coord |= pending_config->state.enabled && pending_config->state.lx == 0 &&
                               pending_config->state.ly == 0;
            have_auto_coord = pending_config->state.enabled && pending_config->state.lx == -1 &&
                              pending_config->state.ly == -1;
            if (have_auto_coord) {
                auto_coord_outputs++;
            }
        } else {
            struct kywc_output *kywc_output = &output->base;
            have_enabled_output |= kywc_output->state.enabled;
            have_zero_coord |= kywc_output->state.enabled && kywc_output->state.lx == 0 &&
                               kywc_output->state.ly == 0;
        }

        /* fixup primary output */
        if (have_enabled_output && need_fix_primary_output) {
            kywc_log(KYWC_WARN, "Fixup primary output to %s", output->base.name);
            need_fix_primary_output = false;
            output_set_pending_primary(output);
        }

        if (have_enabled_output && have_zero_coord) {
            break;
        }
    }

    if (!have_zero_coord && auto_coord_outputs == wl_list_length(&output_manager->output_configs)) {
        have_zero_coord = true;
        pending_config->state.lx = pending_config->state.ly = 0;
    }

    if (!have_enabled_output || (!have_zero_coord && !have_auto_coord)) {
        kywc_log(KYWC_WARN, "All outputs will be disabled or no zero coord, reject this configure");
        goto failed;
    }

    /* 2.config outputs */
    /* call kywc_output_set_state in all pending outputs, enable first and disable outputs later */
    wl_list_for_each(pending_config, &output_manager->output_configs, link) {
        if (!pending_config->state.enabled) {
            continue;
        }
        struct kywc_output *kywc_output = &pending_config->output->base;
        if (!kywc_output_set_state(kywc_output, &pending_config->state)) {
            goto error;
        }
    }

    kywc_output_set_primary(output_manager->pending_primary);

    wl_list_for_each(pending_config, &output_manager->output_configs, link) {
        if (pending_config->state.enabled) {
            continue;
        }
        struct kywc_output *kywc_output = &pending_config->output->base;
        if (!kywc_output_set_state(kywc_output, &pending_config->state)) {
            goto error;
        }
    }

    if (output_manager->fallback_output && output_manager->fallback_output->state.enabled) {
        fallback_output_set_state(output_manager->fallback_output, false);
    }

    if (kywc_log_get_level() >= KYWC_INFO) {
        wl_list_for_each(pending_config, &output_manager->output_configs, link) {
            struct kywc_output *kywc_output = &pending_config->output->base;
            kywc_log(KYWC_INFO,
                     "\t output %s: mode (%d x %d @ %d) scale %f pos (%d, %d) transform %d %s %s",
                     kywc_output->name, kywc_output->state.width, kywc_output->state.height,
                     kywc_output->state.refresh, kywc_output->state.scale, kywc_output->state.lx,
                     kywc_output->state.ly, kywc_output->state.transform,
                     kywc_output->state.enabled ? "enabled" : "disabled",
                     output_manager->primary_output == kywc_output ? "primary" : "");
        }
    }

    ret = true;
error:
    output_manager_emit_configured();
failed:
    output_manager->pending_primary = NULL;

    wl_list_for_each_safe(pending_config, temp, &output_manager->output_configs, link) {
        free(pending_config);
    }
    wl_list_init(&output_manager->output_configs);

    return ret;
}

void output_manager_add_output_pending_state(struct output *output, struct kywc_output_state *state)
{
    struct output_pending_config *pending_config = NULL;
    wl_list_for_each(pending_config, &output_manager->output_configs, link) {
        if (pending_config->output == output) {
            pending_config->state = *state;
            return;
        }
    }

    pending_config = calloc(1, sizeof(*pending_config));
    if (!pending_config) {
        return;
    }
    pending_config->output = output;

    pending_config->state = *state;
    /* copy colortemp and brightness to pending */
    pending_config->state.color_temp = output->base.state.color_temp;
    pending_config->state.brightness = output->base.state.brightness;

    kywc_log(KYWC_DEBUG,
             "%s pending_configs: mode (%d x %d @ %d) scale %f pos (%d, %d) transform %d %s %s",
             output->base.name, state->width, state->height, state->refresh, state->scale,
             state->lx, state->ly, state->transform, state->enabled ? "enabled" : "disabled",
             output_manager->pending_primary == &output->base ? "primary" : "");
    wl_list_insert(&output_manager->output_configs, &pending_config->link);
}

struct output_manager *output_manager_create(struct server *server)
{
    output_manager = calloc(1, sizeof(struct output_manager));
    if (!output_manager) {
        return NULL;
    }

    output_manager->server = server;
    wl_list_init(&output_manager->outputs);
    wl_list_init(&output_manager->output_configs);
    wl_signal_init(&output_manager->events.new_output);
    wl_signal_init(&output_manager->events.primary_output);
    wl_signal_init(&output_manager->events.configured);

    output_manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &output_manager->server_destroy);

    output_manager->server_suspend.notify = handle_server_suspend;
    wl_signal_add(&server->events.suspend, &output_manager->server_suspend);

    output_manager->server_resume.notify = handle_server_resume;
    wl_signal_add(&server->events.resume, &output_manager->server_resume);

    xdg_output_manager_v1_create(server);
    output_manager->new_output.notify = handle_new_output;
    wl_signal_add(&server->backend->events.new_output, &output_manager->new_output);

    struct wlr_output *wlr_output = wlr_headless_add_output(server->headless_backend, 1920, 1080);
    wlr_output_set_name(wlr_output, "FALLBACK");

    output_manager_config_init(output_manager);
    output_manager_layout_init(output_manager);

    kde_output_management_create(server);
    wlr_output_management_create(server);

    return output_manager;
}

uint32_t output_manager_for_each_output(output_iterator_func_t iterator, bool enabled, void *data)
{
    int index = 0;
    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (output->base.state.enabled == enabled) {
            iterator(&output->base, index++, data);
        }
    }
    return index;
}

void output_set_pending_primary(struct output *output)
{
    struct kywc_output *kywc_output = &output->base;
    if (output_manager->pending_primary == kywc_output) {
        return;
    }

    output_manager->pending_primary = kywc_output;
}

void kywc_output_set_primary(struct kywc_output *kywc_output)
{
    if (output_manager->primary_output == kywc_output) {
        return;
    }

    kywc_log(KYWC_INFO, "primary output is changed to %s",
             kywc_output ? kywc_output->name : "none");
    output_manager->primary_output = kywc_output;
    wl_signal_emit_mutable(&output_manager->events.primary_output, kywc_output);
}

struct kywc_output *kywc_output_get_primary(void)
{
    return output_manager->primary_output;
}

void kywc_output_add_new_listener(struct wl_listener *listener)
{
    wl_signal_add(&output_manager->events.new_output, listener);
}

void kywc_output_add_primary_listener(struct wl_listener *listener)
{
    wl_signal_add(&output_manager->events.primary_output, listener);
}

void output_manager_add_configured_listener(struct wl_listener *listener)
{
    wl_signal_add(&output_manager->events.configured, listener);
}

void output_manager_emit_configured(void)
{
    wl_signal_emit_mutable(&output_manager->events.configured, NULL);
}

float kywc_output_preferred_scale(struct kywc_output *kywc_output, int width, int height)
{
    float scale = 1.0;

    if (kywc_output->prop.phys_width == 0 || kywc_output->prop.phys_height == 0) {
        return scale;
    }

    float phys_width = kywc_output->prop.phys_width / 25.4;
    float phys_height = kywc_output->prop.phys_height / 25.4;

    float phys_inch = sqrtf(pow(phys_width, 2) + pow(phys_height, 2));
    float pixels = sqrtf(pow(width, 2) + pow(height, 2));
    float ppi = pixels / phys_inch;

    kywc_log(KYWC_DEBUG, "Output %s inch: %f, resolution: %dx%d, ppi: %f", kywc_output->name,
             phys_inch, width, height, ppi);

    if (ppi > 140.0) {
        float range = ppi - 140.0;
        int step = range / 40.0;
        scale = 1.0 + (step * 0.25);
        if (range >= step * 40) {
            scale += 0.25;
        }
    }

    return scale;
}

struct kywc_output_mode *kywc_output_preferred_mode(struct kywc_output *kywc_output)
{
    struct kywc_output_mode *mode;
    wl_list_for_each(mode, &kywc_output->prop.modes, link) {
        if (mode->preferred) {
            return mode;
        }
    }
    // No preferred mode, choose the first one
    return wl_container_of(kywc_output->prop.modes.prev, mode, link);
}

struct kywc_output *kywc_output_from_resource(struct wl_resource *resource)
{
    struct wlr_output *wlr_output = wl_resource_get_user_data(resource);
    struct output *output = output_from_wlr_output(wlr_output);

    return output ? &output->base : NULL;
}

static void output_find_best_mode(struct wlr_output *wlr_output, int32_t width, int32_t height,
                                  int32_t refresh, struct wlr_output_mode **best)
{
    /* find best mode */
    struct wlr_output_mode *m;
    wl_list_for_each(m, &wlr_output->modes, link) {
        if (width != m->width || height != m->height) {
            continue;
        }
        if (refresh == m->refresh) {
            *best = m;
            break;
        }
        if (!*best || m->refresh > (*best)->refresh) {
            *best = m;
        }
    }
}

static void output_ensure_mode(struct wlr_output *wlr_output, struct wlr_output_state *wlr_state,
                               struct wlr_output_mode *mode)
{
    if (wlr_output_test_state(wlr_output, wlr_state)) {
        return;
    }

    kywc_log(KYWC_ERROR, "mode rejected, falling back to another mode");
    struct wlr_output_mode *m;
    wl_list_for_each(m, &wlr_output->modes, link) {
        if (mode && mode == m) {
            continue;
        }

        wlr_output_state_set_mode(wlr_state, m);
        if (wlr_output_test_state(wlr_output, wlr_state)) {
            break;
        }
    }
}

static bool output_compare_state(struct output *output, const struct kywc_output_state *state)
{
    struct kywc_output_state *current_state = &output->base.state;

    bool changed = current_state->enabled != state->enabled;
    changed |= current_state->power != state->power;
    changed |= current_state->width != state->width || current_state->height != state->height ||
               current_state->refresh != state->refresh;
    changed |= current_state->transform != state->transform;
    changed |= current_state->vrr_policy != state->vrr_policy;
    changed |= current_state->scale != state->scale;
    changed |= output->base.prop.gamma_size < 1;
    changed |= output->base.prop.gamma_size > 1 && current_state->color_temp != state->color_temp;

    return changed;
}

static bool output_set_state(struct output *output, struct kywc_output_state *state)
{
    struct wlr_output *wlr_output = output->wlr_output;
    struct server *server = output_manager->server;

    bool changed = !output->modeset || output_compare_state(output, state);
    if (changed) {
        bool enabled = state->enabled && state->power;
        struct wlr_output_state wlr_state;
        wlr_output_state_init(&wlr_state);
        wlr_output_state_set_enabled(&wlr_state, enabled);

        if (enabled) {
            struct wlr_output_mode *best = NULL;
            if (state->width <= 0 || state->height <= 0) {
                kywc_log(KYWC_INFO, "set preferred mode as no config found");
                best = wlr_output_preferred_mode(wlr_output);
            } else {
                output_find_best_mode(wlr_output, state->width, state->height, state->refresh,
                                      &best);
            }

            if (best) {
                wlr_output_state_set_mode(&wlr_state, best);
            } else {
                wlr_output_state_set_custom_mode(&wlr_state, state->width, state->height,
                                                 state->refresh);
            }
            output_ensure_mode(wlr_output, &wlr_state, best);

            wlr_output_state_set_transform(&wlr_state, state->transform);
            wlr_output_state_set_scale(&wlr_state, state->scale);

            if (output->base.prop.gamma_size > 1) {
                output_set_gamma_lut(wlr_output, output->base.prop.gamma_size, &wlr_state,
                                     state->color_temp);
            }
        }

        if (!wlr_output_commit_state(wlr_output, &wlr_state)) {
            kywc_log(KYWC_ERROR, "Failed to commit output: %s", wlr_output->name);
            wlr_output_state_finish(&wlr_state);
            return false;
        }
        wlr_output_state_finish(&wlr_state);
    }
    /* after output commit, we get actual status */
    struct wlr_output_layout_output *loutput = wlr_output_layout_get(server->layout, wlr_output);
    bool need_layout = state->enabled;
    bool have_layout = !!loutput;
    bool going_on = need_layout && !have_layout;
    bool going_off = !need_layout && have_layout;

    /* if output disabled, skip output_layout_add */
    if (going_on && (state->lx == -1 || state->ly == -1)) {
        loutput = wlr_output_layout_add_auto(server->layout, wlr_output);
    } else if (going_on) {
        loutput = wlr_output_layout_add(server->layout, wlr_output, state->lx, state->ly);
    } else if (going_off) {
        ky_scene_output_destroy(output->scene_output);
        wlr_output_layout_remove(server->layout, wlr_output);
        output->scene_output = NULL;
    } else if (need_layout && have_layout && (loutput->x != state->lx || loutput->y != state->ly)) {
        /* if output logical size changed, layout_change alreay is emited in
         * output_commit. only need move when (x, y) of output is different.
         */
        wlr_output_layout_add(server->layout, wlr_output, state->lx, state->ly);
    }

    if (going_on && loutput) {
        output->scene_output = ky_scene_output_create(server->scene, wlr_output);
        ky_scene_output_layout_add_output(server->scene_layout, loutput, output->scene_output);
    }

    return true;
}

static void output_update_geometry(struct output *output, struct kywc_box *box)
{
    struct kywc_output_state *state = &output->base.state;

    if (state->transform % 2 == 0) {
        box->width = state->width;
        box->height = state->height;
    } else {
        box->width = state->height;
        box->height = state->width;
    }

    box->x = state->lx;
    box->y = state->ly;
    box->width /= state->scale;
    box->height /= state->scale;
}

static void output_update_usable_area(struct output *output, struct kywc_box *usable)
{
    *usable = output->geometry;
    wl_signal_emit_mutable(&output->events.update_usable_area, usable);
    wl_signal_emit_mutable(&output->events.update_late_usable_area, usable);
}

void kywc_output_update_usable_area(struct kywc_output *kywc_output)
{
    /* no need to update usable area when disabled or destroyed */
    if (!kywc_output || kywc_output->destroying || !kywc_output->state.enabled) {
        return;
    }

    struct output *output = output_from_kywc_output(kywc_output);
    struct kywc_box usable_area;

    output_update_usable_area(output, &usable_area);
    if (kywc_box_equal(&output->usable_area, &usable_area)) {
        return;
    }

    output->usable_area = usable_area;
    kywc_log(KYWC_DEBUG, "output %s usable area is (%d, %d) %d x %d", output->base.name,
             output->usable_area.x, output->usable_area.y, output->usable_area.width,
             output->usable_area.height);
    wl_signal_emit_mutable(&output->events.usable_area, NULL);
}

bool kywc_output_set_state(struct kywc_output *kywc_output, struct kywc_output_state *state)
{
    struct output *output = output_from_kywc_output(kywc_output);
    if (!output_set_state(output, state)) {
        return false;
    }

    struct kywc_output_state *current = &kywc_output->state;
    struct kywc_output_state old = kywc_output->state;
    output_get_state(output, current);

    // XXX: fix current.enabled for dpms power
    current->enabled = state->enabled;
    if (kywc_output->prop.gamma_size > 1) {
        current->color_temp = state->color_temp;
    }

    /* fix gamma supoort by get gamma_size again */
    kywc_output->prop.gamma_size = wlr_output_get_gamma_size(output->wlr_output);

    /* update geometry and usable area before all signals */
    bool geometry_changed = false;
    bool usable_area_changed = false;

    if (current->enabled) {
        struct kywc_box geo = output->geometry;
        output_update_geometry(output, &output->geometry);
        geometry_changed = !kywc_box_equal(&geo, &output->geometry);
        /* only update usable area when geometry changed */
        if (!old.enabled || geometry_changed) {
            output_update_usable_area(output, &geo);
            if (!kywc_box_equal(&geo, &output->usable_area)) {
                output->usable_area = geo;
                usable_area_changed = true;
            }
        }
    }

    /* check state changes */
    if (current->enabled != old.enabled) {
        if (!current->enabled) {
            wl_signal_emit_mutable(&kywc_output->events.off, NULL);
            output_write_config(output);
            return true;
        } else {
            wl_signal_emit_mutable(&kywc_output->events.on, NULL);
        }
    }

    if (geometry_changed) {
        kywc_log(KYWC_DEBUG, "output %s geometry is (%d, %d) %d x %d", output->base.name,
                 output->geometry.x, output->geometry.y, output->geometry.width,
                 output->geometry.height);
        wl_signal_emit_mutable(&output->events.geometry, NULL);
    }

    if (usable_area_changed) {
        kywc_log(KYWC_DEBUG, "output %s usable area is (%d, %d) %d x %d", output->base.name,
                 output->usable_area.x, output->usable_area.y, output->usable_area.width,
                 output->usable_area.height);
        wl_signal_emit_mutable(&output->events.usable_area, NULL);
    }

    if (current->power != old.power) {
        wl_signal_emit_mutable(&kywc_output->events.power, NULL);
    }

    if (current->width != old.width || current->height != old.height ||
        current->refresh != old.refresh) {
        wl_signal_emit_mutable(&kywc_output->events.mode, NULL);
    }

    if (current->transform != old.transform) {
        wl_signal_emit_mutable(&kywc_output->events.transform, NULL);
    }

    if (current->scale != old.scale) {
        wl_signal_emit_mutable(&kywc_output->events.scale, NULL);
    }

    if (current->lx != old.lx || current->ly != old.ly) {
        wl_signal_emit_mutable(&kywc_output->events.position, NULL);
    }

    output_write_config(output);

    return true;
}

struct kywc_output *kywc_output_by_name(const char *name)
{
    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (!strcmp(name, output->base.name)) {
            return &output->base;
        }
    }

    return NULL;
}

void kywc_output_effective_geometry(struct kywc_output *kywc_output, struct kywc_box *box)
{
    struct output *output = output_from_kywc_output(kywc_output);

    *box = output->geometry;
}

bool kywc_output_contains_point(struct kywc_output *kywc_output, int x, int y)
{
    struct output *output = output_from_kywc_output(kywc_output);

    return kywc_box_contains_point(&output->geometry, x, y);
}

void output_add_update_usable_area_listener(struct kywc_output *kywc_output,
                                            struct wl_listener *listener, bool late)
{
    struct output *output = output_from_kywc_output(kywc_output);

    if (late) {
        wl_signal_add(&output->events.update_late_usable_area, listener);
    } else {
        wl_signal_add(&output->events.update_usable_area, listener);
    }
}

static void output_edge_position(struct output *output, enum layout_edge edge, int *lx, int *ly)
{
    struct kywc_box *geo = &output->geometry;

    switch (edge) {
    case LAYOUT_EDGE_TOP:
        *lx = geo->x + geo->width / 2;
        *ly = geo->y - 1;
        break;
    case LAYOUT_EDGE_BOTTOM:
        *lx = geo->x + geo->width / 2;
        *ly = geo->y + geo->height;
        break;
    case LAYOUT_EDGE_LEFT:
        *lx = geo->x - 1;
        *ly = geo->y + geo->height / 2;
        break;
    case LAYOUT_EDGE_RIGHT:
        *lx = geo->x + geo->width;
        *ly = geo->y + geo->height / 2;
        break;
    }
}

bool output_at_layout_edge(struct output *output, enum layout_edge edge)
{
    int lx = 0, ly = 0;
    output_edge_position(output, edge, &lx, &ly);

    struct wlr_output_layout *layout = output->manager->server->layout;
    return !wlr_output_layout_contains_point(layout, NULL, lx, ly);
}

struct output *output_adjacent_output(struct output *output, enum layout_edge edge)
{
#if 1
    int lx = 0, ly = 0;
    output_edge_position(output, edge, &lx, &ly);

    struct wlr_output_layout *layout = output_manager->server->layout;
    struct wlr_output *wlr_output = wlr_output_layout_output_at(layout, lx, ly);
    return wlr_output ? output_from_wlr_output(wlr_output) : NULL;
#else
    struct wlr_output_layout *layout = output_manager->server->layout;
    struct wlr_output *wlr_output = NULL;

    switch (edge) {
    case LAYOUT_EDGE_LEFT:
        wlr_output =
            wlr_output_layout_adjacent_output(layout, WLR_DIRECTION_LEFT, output->wlr_output, 1, 0);
        break;
    case LAYOUT_EDGE_RIGHT:
        wlr_output = wlr_output_layout_adjacent_output(layout, WLR_DIRECTION_RIGHT,
                                                       output->wlr_output, 1, 0);
        break;
    case LAYOUT_EDGE_TOP:
        wlr_output =
            wlr_output_layout_adjacent_output(layout, WLR_DIRECTION_UP, output->wlr_output, 0, 1);
        break;
    case LAYOUT_EDGE_BOTTOM:
        wlr_output =
            wlr_output_layout_adjacent_output(layout, WLR_DIRECTION_DOWN, output->wlr_output, 0, 1);
        break;
    }

    return wlr_output ? output_from_wlr_output(wlr_output) : NULL;
#endif
}

struct kywc_output *kywc_output_at_point(double lx, double ly)
{
    struct wlr_output_layout *layout = output_manager->server->layout;
    struct wlr_output *wlr_output = NULL;
    double closest_x, closest_y;

    wlr_output_layout_closest_point(layout, wlr_output, lx, ly, &closest_x, &closest_y);
    wlr_output = wlr_output_layout_output_at(layout, closest_x, closest_y);

    return wlr_output ? &output_from_wlr_output(wlr_output)->base : NULL;
}
