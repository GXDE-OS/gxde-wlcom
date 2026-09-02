// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <wlr/backend/drm.h>
#include <wlr/backend/headless.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/region.h>

#include <kywc/log.h>

#include "backend/fbdev.h"
#include "effect/output_transform.h"
#include "output_p.h"
#include "render/profile.h"
#include "util/debug.h"
#include "util/quirks.h"
#include "xwayland.h"

static struct output_manager *output_manager = NULL;
static char *unknown = " ";

#define HOTPLUG_MODE_UPDATE_INTERVAL_MS 250

static bool output_sync_committed_state(struct output *output, bool enabled);

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
    wl_list_remove(&output_manager->configured.link);

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

static const char *output_get_edid(struct wlr_output *wlr_output)
{
    if (!wlr_output_is_drm(wlr_output)) {
        return NULL;
    }

    int drm_fd = wlr_drm_backend_get_non_master_fd(wlr_output->backend);
    if (drm_fd < 0) {
        return NULL;
    }

    uint32_t conn_id = wlr_drm_connector_get_id(wlr_output);

    drmModeObjectProperties *props =
        drmModeObjectGetProperties(drm_fd, conn_id, DRM_MODE_OBJECT_CONNECTOR);
    if (!props) {
        kywc_log_errno(KYWC_ERROR, "Failed to get DRM object properties");
        close(drm_fd);
        return NULL;
    }

    const char *edid = NULL;
    for (uint32_t i = 0; i < props->count_props; ++i) {
        drmModePropertyRes *prop = drmModeGetProperty(drm_fd, props->props[i]);
        if (!prop) {
            kywc_log_errno(KYWC_ERROR, "Failed to get DRM object property");
            continue;
        }

        if (!strcmp(prop->name, "EDID")) {
            drmModePropertyBlobRes *blob = drmModeGetPropertyBlob(drm_fd, props->prop_values[i]);
            if (blob) {
                edid = kywc_identifier_base64_generate(blob->data, blob->length);
                drmModeFreePropertyBlob(blob);
            }
            drmModeFreeProperty(prop);
            break;
        }

        drmModeFreeProperty(prop);
    }

    drmModeFreeObjectProperties(props);

    close(drm_fd);

    return edid;
}

static bool get_drm_prop(int fd, uint32_t obj, uint32_t prop, uint64_t *ret)
{
    drmModeObjectProperties *props = drmModeObjectGetProperties(fd, obj, DRM_MODE_OBJECT_ANY);
    if (!props) {
        return false;
    }

    bool found = false;
    for (uint32_t i = 0; i < props->count_props; ++i) {
        if (props->props[i] == prop) {
            *ret = props->prop_values[i];
            found = true;
            break;
        }
    }

    drmModeFreeObjectProperties(props);

    return found;
}

static bool output_get_vrr_capable(struct wlr_output *wlr_output)
{
    if (!wlr_output_is_drm(wlr_output)) {
        return false;
    }

    int drm_fd = wlr_drm_backend_get_non_master_fd(wlr_output->backend);
    if (drm_fd < 0) {
        return false;
    }

    uint32_t conn_id = wlr_drm_connector_get_id(wlr_output);

    drmModeObjectProperties *props =
        drmModeObjectGetProperties(drm_fd, conn_id, DRM_MODE_OBJECT_CONNECTOR);
    if (!props) {
        kywc_log_errno(KYWC_ERROR, "Failed to get DRM object properties");
        close(drm_fd);
        return false;
    }

    uint64_t vrr_capable = 0;
    for (uint32_t i = 0; i < props->count_props; ++i) {
        drmModePropertyRes *prop = drmModeGetProperty(drm_fd, props->props[i]);
        if (!prop) {
            kywc_log_errno(KYWC_ERROR, "Failed to get DRM object property");
            continue;
        }
        if (!strcmp(prop->name, "vrr_capable")) {
            get_drm_prop(drm_fd, conn_id, prop->prop_id, &vrr_capable);
            kywc_log(KYWC_INFO, "output: %s, VRR-Capable: %ld", wlr_output->name, vrr_capable);
            drmModeFreeProperty(prop);
            break;
        }

        drmModeFreeProperty(prop);
    }

    drmModeFreeObjectProperties(props);

    close(drm_fd);

    return vrr_capable != 0;
}

static void output_get_prop(struct output *output, struct kywc_output_prop *prop)
{
    struct wlr_output *wlr_output = output->wlr_output;

    prop->phys_width = wlr_output->phys_width;
    prop->phys_height = wlr_output->phys_height;
    prop->make = wlr_output->make;
    prop->model = wlr_output->model;
    prop->serial = wlr_output->serial;
    prop->desc = wlr_output->description;
    prop->is_virtual = wlr_output_is_headless(wlr_output);
    prop->is_fbdev = wlr_output_is_fbdev(wlr_output);
    prop->gamma_size = wlr_output_get_gamma_size(wlr_output);

    prop->capabilities = KYWC_OUTPUT_CAPABILITY_POWER;
    if (!prop->is_fbdev) {
        prop->capabilities |= KYWC_OUTPUT_CAPABILITY_BRIGHTNESS | KYWC_OUTPUT_CAPABILITY_COLOR_TEMP;
    }
    if (output_get_vrr_capable(wlr_output)) {
        prop->capabilities |= KYWC_OUTPUT_CAPABILITY_VRR;
    }

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

    struct wlr_output_layout_output *layout_output;
    layout_output = wlr_output_layout_get(output_manager->server->layout, wlr_output);
    if (layout_output) {
        state->lx = layout_output->x;
        state->ly = layout_output->y;
    } else {
        state->lx = state->ly = -1;
    }

    state->brightness = output->brightness;
    state->color_temp = output->color_temp;
    state->vrr_policy = output->vrr_policy;
}

static void fallback_output_set_state(struct kywc_output *kywc_output, bool enabled,
                                      struct kywc_output *mirror)
{
    if (!kywc_output->state.enabled && !enabled) {
        return;
    }

    struct kywc_output_state state = kywc_output->state;
    state.enabled = enabled;

    if (enabled) {
        state.power = true;
        state.lx = state.ly = 0;

        if (mirror) {
            state.scale = mirror->state.scale;
            state.transform = mirror->state.transform;
            state.width = mirror->state.width;
            state.height = mirror->state.height;
            state.refresh = mirror->state.refresh;
        } else {
            state.scale = 1.0;
            state.transform = WL_OUTPUT_TRANSFORM_NORMAL;
            struct kywc_output_mode *mode = kywc_output_preferred_mode(kywc_output);
            state.width = mode->width;
            state.height = mode->height;
            state.refresh = mode->refresh;
        }
    }

    kywc_output_set_state(kywc_output, &state);
}

static void output_init_quirks(struct output *output)
{
    if (!wlr_output_is_drm(output->wlr_output)) {
        return;
    }

    int drm_fd = wlr_drm_backend_get_non_master_fd(output->wlr_output->backend);
    output->quirks = quirks_by_backend(drm_fd);

    /*  using software curosr, depending on the quirks mask */
    if (output->quirks & QUIRKS_MASK_SOFTWARE_CURSOR) {
        wlr_output_lock_software_cursors(output->wlr_output, true);
    }

    close(drm_fd);
}

static void output_uuid_generate(struct kywc_output *kywc_output)
{
    char description[128];
    snprintf(description, sizeof(description), "%s %s%s%s (%s)", kywc_output->prop.make,
             kywc_output->prop.model, kywc_output->prop.serial ? " " : "",
             kywc_output->prop.serial ? kywc_output->prop.serial : "", kywc_output->name);
    kywc_output->uuid = kywc_identifier_md5_generate_uuid((void *)description, strlen(description));
}

static int handle_hotplug_mode_update(void *data)
{
    struct output *output = data;
    drmModeConnector *connector =
        drmModeGetConnector(output->drm_fd, output->drm_connector_id);
    if (!connector) {
        goto out;
    }

    const drmModeModeInfo *preferred = NULL;
    for (int i = 0; i < connector->count_modes; ++i) {
        if (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            preferred = &connector->modes[i];
            break;
        }
    }

    if (!preferred || !output->base.state.enabled ||
        (output->base.state.width == preferred->hdisplay &&
         output->base.state.height == preferred->vdisplay)) {
        drmModeFreeConnector(connector);
        goto out;
    }

    /*
     * VM drivers replace the preferred connector mode without unplugging the
     * output.  wlroots 0.17 deliberately leaves driver-specific hotplug policy
     * to compositors, so import that preferred mode as a user mode and commit
     * it through the normal output state path.
     */
    drmModeModeInfo mode_info = *preferred;
    mode_info.type = DRM_MODE_TYPE_USERDEF;
    struct wlr_output_mode *mode =
        wlr_drm_connector_add_mode(output->wlr_output, &mode_info);
    drmModeFreeConnector(connector);
    if (!mode) {
        kywc_log(KYWC_ERROR, "failed to import host preferred mode for output %s",
                 output->base.name);
        goto out;
    }

    struct wlr_output_mode *wlr_mode;
    wl_list_for_each(wlr_mode, &output->wlr_output->modes, link) {
        wlr_mode->preferred = wlr_mode == mode;
    }

    bool mode_listed = false;
    struct kywc_output_mode *kywc_mode;
    wl_list_for_each(kywc_mode, &output->base.prop.modes, link) {
        kywc_mode->preferred = kywc_mode->width == mode->width &&
            kywc_mode->height == mode->height && kywc_mode->refresh == mode->refresh;
        mode_listed |= kywc_mode->preferred;
    }
    if (!mode_listed) {
        output_add_mode(&output->base.prop, mode);
    }

    struct kywc_output_state state = output->base.state;
    state.width = mode->width;
    state.height = mode->height;
    state.refresh = mode->refresh;

    kywc_log(KYWC_INFO, "following host preferred mode on output %s: %d x %d @ %d",
             output->base.name, state.width, state.height, state.refresh);
    if (kywc_output_set_state(&output->base, &state)) {
        output_manager_emit_configured(CONFIGURE_TYPE_UPDATE);
    }

out:
    wl_event_source_timer_update(output->hotplug_mode_update_timer,
                                 HOTPLUG_MODE_UPDATE_INTERVAL_MS);
    return 0;
}

static void output_init_hotplug_mode_update(struct output *output)
{
    if (!wlr_output_is_drm(output->wlr_output)) {
        return;
    }

    int drm_fd = wlr_drm_backend_get_non_master_fd(output->wlr_output->backend);
    if (drm_fd < 0) {
        return;
    }

    uint32_t connector_id = wlr_drm_connector_get_id(output->wlr_output);
    drmModeObjectProperties *props =
        drmModeObjectGetProperties(drm_fd, connector_id, DRM_MODE_OBJECT_CONNECTOR);
    if (!props) {
        close(drm_fd);
        return;
    }

    bool follows_host_mode = false;
    for (uint32_t i = 0; i < props->count_props; ++i) {
        drmModePropertyRes *prop = drmModeGetProperty(drm_fd, props->props[i]);
        if (!prop) {
            continue;
        }

        if (strcmp(prop->name, "hotplug_mode_update") == 0) {
            follows_host_mode = props->prop_values[i] != 0;
            drmModeFreeProperty(prop);
            break;
        }
        drmModeFreeProperty(prop);
    }
    drmModeFreeObjectProperties(props);

    if (!follows_host_mode) {
        close(drm_fd);
        return;
    }

    output->drm_fd = drm_fd;
    output->drm_connector_id = connector_id;
    output->hotplug_mode_update_timer = wl_event_loop_add_timer(
        output_manager->server->event_loop, handle_hotplug_mode_update, output);
    if (!output->hotplug_mode_update_timer) {
        close(drm_fd);
        output->drm_fd = -1;
        return;
    }

    kywc_log(KYWC_INFO, "output %s follows host-provided preferred modes", output->base.name);
    wl_event_source_timer_update(output->hotplug_mode_update_timer,
                                 HOTPLUG_MODE_UPDATE_INTERVAL_MS);
}

/* the actual enabled output except for fallback */
static bool output_manager_has_enabled_outputs(void)
{
    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (&output->base == output_manager->fallback_output) {
            continue;
        }
        if (output->base.state.enabled) {
            return true;
        }
    }
    return false;
}

bool output_manager_has_actual_outputs(void)
{
    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (&output->base != output_manager->fallback_output) {
            return true;
        }
    }
    return false;
}

static struct output *output_create(const char *name, struct wlr_output *wlr_output)
{
    struct output *output = calloc(1, sizeof(struct output));
    if (!output) {
        return NULL;
    }

    output->drm_fd = -1;
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
    wl_signal_init(&kywc_output->events.brightness);
    wl_signal_init(&kywc_output->events.color_temp);
    wl_signal_init(&kywc_output->events.destroy);

    wl_signal_init(&output->events.disable);
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
        kywc_output->prop.make = name;
    }
    if (kywc_output->prop.is_virtual && strcmp(name, "FALLBACK") == 0) {
        output_manager->fallback_output = kywc_output;
    }

    output->manager = output_manager;
    wl_list_insert(&output_manager->outputs, &output->link);

    output_get_state(output, &kywc_output->state);

    kywc_output->edid = output_get_edid(output->wlr_output);
    output_uuid_generate(kywc_output);
    kywc_log(KYWC_INFO, "new output %s: %s", kywc_output->name, kywc_output->uuid);

    /* read config and apply it */
    struct kywc_output_state pending = kywc_output->state;
    bool found = output_read_config(output, &pending);
    found = output_manager->has_layout_manager ? found : false;
    /* use parttial config(scale,brightness,color_temp) */
    if (!found) {
        pending.enabled = pending.power = true;
        if (output_manager->fallback_output == kywc_output) {
            pending.lx = pending.ly = 0;
        } else {
            pending.lx = pending.ly = -1;
        }
        pending.brightness = pending.brightness == 0 ? 100 : pending.brightness;
        pending.color_temp = pending.color_temp == 0 ? 6500 : pending.color_temp;

        struct kywc_output_mode *mode = kywc_output_preferred_mode(kywc_output);
        pending.width = mode->width;
        pending.height = mode->height;
        pending.refresh = mode->refresh;
    }

    if (!output_manager->has_layout_manager || output_manager->fallback_output == kywc_output) {
        output_manager_add_output_pending_state(output, &pending);
        if (pending.primary) {
            output_set_pending_primary(output);
        }
    } else {
        output_manager_get_layout_configs(output_manager);
        /* copy colortemp and brightness to pending */
        struct output_pending_config *pending_config = NULL;
        wl_list_for_each(pending_config, &output_manager->output_configs, link) {
            if (pending_config->output == output) {
                pending_config->state.brightness = pending.brightness;
                pending_config->state.color_temp = pending.color_temp;
            } else {
                struct kywc_output_state *state = &pending_config->output->base.state;
                pending_config->state.brightness = state->brightness;
                pending_config->state.color_temp = state->color_temp;
            }
        }
    }

    output_manager_configure_outputs();
    output->initialized = true;

    output_init_quirks(output);
    output_init_hotplug_mode_update(output);

    wl_signal_emit_mutable(&output_manager->events.new_output, kywc_output);

    if (output->base.state.enabled) {
        wl_signal_emit_mutable(&output_manager->events.new_enabled_output, kywc_output);
    }

    return output;
}

static void handle_output_present(struct wl_listener *listener, void *data)
{
    KY_PROFILE_ZONE(zone, __func__);

    struct output *output = wl_container_of(listener, output, present);
    output->scene_commit = false;

    KY_PROFILE_RENDER_COLLECT(output->wlr_output->renderer);

    if (!output->has_pending) {
        KY_PROFILE_ZONE_END(zone);
        return;
    }

    if (kywc_output_set_state(&output->base, &output->pending_state)) {
        output_manager_emit_configured(CONFIGURE_TYPE_UPDATE);
        /* has_pending is reset in handle_output_frame */
    } else {
        output->has_pending = false;
    }

    KY_PROFILE_ZONE_END(zone);
}

static void handle_output_commit(struct wl_listener *listener, void *data)
{
    struct output *output = wl_container_of(listener, output, commit);
    const struct wlr_output_event_commit *event = data;
    const uint32_t geometry_state = WLR_OUTPUT_STATE_MODE | WLR_OUTPUT_STATE_SCALE |
        WLR_OUTPUT_STATE_TRANSFORM;

    if (output->setting_state || !(event->state->committed & geometry_state)) {
        return;
    }

    kywc_log(KYWC_INFO, "output %s mode changed by backend to %d x %d @ %d",
             output->base.name, output->wlr_output->width, output->wlr_output->height,
             output->wlr_output->refresh);
    output_sync_committed_state(output, output->wlr_output->enabled);
    output_manager_emit_configured(CONFIGURE_TYPE_UPDATE);
}

static void handle_output_frame(struct wl_listener *listener, void *data)
{
    if (!output_manager->server->active) {
        return;
    }

    struct output *output = wl_container_of(listener, output, frame);

    if (output_manager->primary_output == &output->base) {
        KY_PROFILE_FRAME();
    }
    KY_PROFILE_FRAME_NAME(output->wlr_output->name);

    KY_PROFILE_ZONE(zone, __func__);

    /* skip rendering if has_pending, otherwise drm may report busy */
    if (output->has_pending) {
        output->wlr_output->frame_pending = true;
        output->has_pending = false;
        KY_PROFILE_ZONE_END(zone);
        return;
    }

    output->scene_commit = ky_scene_output_commit(output->scene_output, NULL);

    struct timespec now = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &now);
    ky_scene_output_send_frame_done(output->scene_output, &now);
    KY_PROFILE_ZONE_END(zone);
}

static void output_damage_set_enabled(bool enabled)
{
    if (output_manager->damage_enabled == enabled) {
        return;
    }
    output_manager->damage_enabled = enabled;

    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (enabled) {
            if (wl_list_empty(&output->precommit.link)) {
                wl_signal_add(&output->wlr_output->events.precommit, &output->precommit);
            }
            pixman_region32_init_rect(&output->damage_region, output->geometry.x,
                                      output->geometry.y, output->geometry.width,
                                      output->geometry.height);
            wl_signal_emit_mutable(&output_manager->events.damage, output);
        } else {
            wl_list_remove(&output->precommit.link);
            wl_list_init(&output->precommit.link);
        }
    }
}

static void handle_output_precommit(struct wl_listener *listener, void *data)
{
    KY_PROFILE_ZONE(zone, __func__);

    struct output *output = wl_container_of(listener, output, precommit);
    const struct wlr_output_event_precommit *event = data;
    struct pixman_region32 *region = &output->damage_region;

    if (wl_list_empty(&output_manager->events.damage.listener_list)) {
        output_damage_set_enabled(false);
        KY_PROFILE_ZONE_END(zone);
        return;
    }

    if (event->state->committed & WLR_OUTPUT_STATE_DAMAGE) {
        if (!pixman_region32_not_empty(&event->state->damage)) {
            KY_PROFILE_ZONE_END(zone);
            return;
        }

        // If the compositor submitted damage, copy it over
        pixman_region32_t damage;
        pixman_region32_init(&damage);
        pixman_region32_copy(&damage, &event->state->damage);

        // translate to layout coord
        struct wlr_output *wlr_output = output->wlr_output;
        wlr_region_transform(&damage, &damage, wlr_output->transform, wlr_output->width,
                             wlr_output->height);
        wlr_region_scale(&damage, &damage, 1 / wlr_output->scale);
        pixman_region32_translate(&damage, output->geometry.x, output->geometry.y);

        pixman_region32_union(region, region, &damage);
        pixman_region32_intersect_rect(region, region, output->geometry.x, output->geometry.y,
                                       output->geometry.width, output->geometry.height);
        pixman_region32_fini(&damage);
    } else if (event->state->committed & WLR_OUTPUT_STATE_BUFFER) {
        // If the compositor did not submit damage but did submit a buffer damage everything
        pixman_region32_union_rect(region, region, output->geometry.x, output->geometry.y,
                                   output->geometry.width, output->geometry.height);
    }

    if (!pixman_region32_not_empty(region)) {
        KY_PROFILE_ZONE_END(zone);
        return;
    }

    wl_signal_emit_mutable(&output_manager->events.damage, output);

    pixman_region32_clear(region);

    KY_PROFILE_ZONE_END(zone);
}

static void output_destroy(struct output *output)
{
    struct kywc_output *kywc_output = &output->base;

    kywc_output->destroying = true;
    wl_signal_emit_mutable(&output->events.disable, NULL);
    wl_signal_emit_mutable(&kywc_output->events.destroy, NULL);

    wl_list_remove(&output->link);
    /* get layouts configure outputs */
    output_manager_get_layout_configs(output_manager);
    /* copy colortemp and brightness to pending */
    struct output_pending_config *pending_config = NULL;
    wl_list_for_each(pending_config, &output_manager->output_configs, link) {
        struct kywc_output_state *state = &pending_config->output->base.state;
        pending_config->state.brightness = state->brightness;
        pending_config->state.color_temp = state->color_temp;
    }

    /* fix primary output that is being destroyed */
    if (output_manager->primary_output == &output->base) {
        output_manager->primary_output = NULL;

        if (wl_list_empty(&output_manager->output_configs) &&
            !wl_list_empty(&output_manager->outputs)) {
            struct output *output;
            wl_list_for_each(output, &output_manager->outputs, link) {
                if (!output->base.state.enabled) {
                    continue;
                }
                kywc_output_set_primary(&output->base);
            }
        }
    }

    output_manager_configure_outputs();

    if (!output_manager->server->terminate && output_manager->fallback_output &&
        !output_manager_has_enabled_outputs()) {
        kywc_output_set_primary(output_manager->fallback_output);
        fallback_output_set_state(output_manager->fallback_output, true, kywc_output);
        output_manager_emit_configured(CONFIGURE_TYPE_UPDATE);
    }

    struct kywc_output_mode *mode, *tmp_mode;
    wl_list_for_each_safe(mode, tmp_mode, &kywc_output->prop.modes, link) {
        wl_list_remove(&mode->link);
        free(mode);
    }

    free((void *)kywc_output->edid);
    free((void *)kywc_output->uuid);
    free(output);
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct output *output = wl_container_of(listener, output, destroy);

    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->present.link);
    wl_list_remove(&output->commit.link);

    if (output->hotplug_mode_update_timer) {
        wl_event_source_remove(output->hotplug_mode_update_timer);
    }
    if (output->drm_fd >= 0) {
        close(output->drm_fd);
    }

    /* output may be disabled before */
    if (output->scene_output) {
        ky_scene_output_destroy(output->scene_output);
        wlr_output_layout_remove(output_manager->server->layout, output->wlr_output);
    }

    pixman_region32_fini(&output->damage_region);

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

    output->present.notify = handle_output_present;
    output->frame.notify = handle_output_frame;
    output->destroy.notify = handle_output_destroy;
    output->commit.notify = handle_output_commit;
    wl_signal_add(&wlr_output->events.present, &output->present);
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    wl_signal_add(&wlr_output->events.commit, &output->commit);

    pixman_region32_init_rect(&output->damage_region, output->geometry.x, output->geometry.y,
                              output->geometry.width, output->geometry.height);

    output->precommit.notify = handle_output_precommit;
    if (output_manager->damage_enabled) {
        wl_signal_add(&output->wlr_output->events.precommit, &output->precommit);
    } else {
        wl_list_init(&output->precommit.link);
    }
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
        output_manager_emit_configured(CONFIGURE_TYPE_NONE);
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

struct kywc_output *output_manager_get_fallback(void)
{
    return output_manager->fallback_output;
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

static void output_manager_update_primary_state(void)
{
    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        struct kywc_output *kywc_output = &output->base;
        struct kywc_output_state *state = &kywc_output->state;
        bool primary = kywc_output_get_primary() == kywc_output;
        state->primary = primary;
        output_write_config(output);
    }
}

bool output_manager_configure_outputs(void)
{
    if (wl_list_empty(&output_manager->output_configs)) {
        return false;
    }

    bool ret = false;

    if (output_manager->server->terminate) {
        goto failed;
    }

    /* 1.check configs */
    bool need_fix_primary_output = false;
    if (!output_manager->pending_primary && output_manager->primary_output &&
        output_manager->primary_output != output_manager->fallback_output) {
        output_manager->pending_primary = output_manager->primary_output;
        kywc_log(KYWC_WARN, "Fixup primary output to %s when no pending_primary",
                 output_manager->primary_output->name);
    }
    need_fix_primary_output = !output_manager->pending_primary;

    struct kywc_output *kywc_output = NULL;
    /* primary output may be disabled, fixup it */
    struct output_pending_config *pending_config, *temp;
    wl_list_for_each(pending_config, &output_manager->output_configs, link) {
        /* It's going to disable the primary output and no new primary config */
        kywc_output = &pending_config->output->base;
        if (kywc_output == output_manager->pending_primary && !pending_config->state.enabled) {
            need_fix_primary_output = true;
            break;
        }
    }

    /* if all outputs will be disabled in config, find others not in config */
    bool have_enabled_output = false;
    bool have_zero_coord = false;
    bool have_actual_output = output_manager_has_actual_outputs();

    struct output *output = NULL;
    wl_list_for_each(output, &output_manager->outputs, link) {
        kywc_output = &output->base;
        if (have_actual_output && kywc_output == output_manager->fallback_output) {
            continue;
        }

        if (get_output_pending_config(output)) {
            pending_config = get_output_pending_config(output);
            have_enabled_output |= pending_config->state.enabled;
            have_zero_coord |= pending_config->state.enabled && pending_config->state.lx == 0 &&
                               pending_config->state.ly == 0;
        } else {
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

    /* have one actual output and coord is not zero */
    if (!have_zero_coord && pending_config) {
        int output_num = wl_list_length(&output_manager->outputs);
        if (output_num == 1 ||
            (output_num == 2 && have_actual_output && output_manager->fallback_output)) {
            struct kywc_output *kywc_output = &pending_config->output->base;
            kywc_log(KYWC_WARN, "Fixup output %s coord to zero", kywc_output->name);
            pending_config->state.lx = pending_config->state.ly = 0;
        }
    }

    if (!have_enabled_output) {
        kywc_log(KYWC_WARN, "All outputs will be disabled, reject this configuration");
        goto failed;
    }

    /* 2.config outputs */
    /* call kywc_output_set_state in all pending outputs, enable first and disable outputs later
     */
    wl_list_for_each(pending_config, &output_manager->output_configs, link) {
        if (!pending_config->state.enabled) {
            continue;
        }
        kywc_output = &pending_config->output->base;
        if (!kywc_output_set_state(kywc_output, &pending_config->state)) {
            goto error;
        }
    }

    kywc_output_set_primary(output_manager->pending_primary);

    wl_list_for_each(pending_config, &output_manager->output_configs, link) {
        if (pending_config->state.enabled) {
            continue;
        }
        kywc_output = &pending_config->output->base;
        if (!kywc_output_set_state(kywc_output, &pending_config->state)) {
            goto error;
        }
    }

    /* disable the fallback when an actual enabled output exists */
    if (output_manager->fallback_output && output_manager_has_enabled_outputs()) {
        fallback_output_set_state(output_manager->fallback_output, false, NULL);
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
    output_manager_emit_configured(CONFIGURE_TYPE_UPDATE);
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
    wl_list_insert(&output_manager->output_configs, &pending_config->link);

    kywc_log(KYWC_DEBUG,
             "%s pending_configs: mode (%d x %d @ %d) scale %f pos (%d, %d) transform %d %s %s "
             "brightness %d colortemp %d",
             output->base.name, state->width, state->height, state->refresh, state->scale,
             state->lx, state->ly, state->transform, state->enabled ? "enabled" : "disabled",
             output_manager->pending_primary == &output->base ? "primary" : "", state->brightness,
             state->color_temp);
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
    wl_list_init(&output_manager->configured.link);
    wl_signal_init(&output_manager->events.new_output);
    wl_signal_init(&output_manager->events.new_enabled_output);
    wl_signal_init(&output_manager->events.primary_output);
    wl_signal_init(&output_manager->events.configured);
    wl_signal_init(&output_manager->events.damage);

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
    output_manager->has_layout_manager = output_manager_layout_init(output_manager);

    ky_output_manager_create(server);
    kde_output_management_create(server);
    wlr_output_management_create(server);
    ukui_output_management_create(server);

    char *env = getenv("KYWC_SOFTWARE_GAMMA");
    output_manager->force_software_gamma = (env && strcmp(env, "1") == 0);

    return output_manager;
}

uint32_t output_manager_for_each_output(output_iterator_func_t iterator, bool enabled, void *data)
{
    int index = 0;
    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (output->base.state.enabled == enabled) {
            if (iterator(&output->base, index++, data)) {
                break;
            }
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
    output_manager_update_primary_state();
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

void output_manager_emit_configured(enum configure_type type)
{
    struct configure_event event = { .type = type };
    wl_signal_emit_mutable(&output_manager->events.configured, &event);
}

void output_manager_add_damage_listener(struct wl_listener *listener)
{
    wl_signal_add(&output_manager->events.damage, listener);

    output_damage_set_enabled(true);
}

void output_manager_add_new_enabled_listener(struct wl_listener *listener)
{
    wl_signal_add(&output_manager->events.new_enabled_output, listener);
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

struct output *output_from_resource(struct wl_resource *resource)
{
    struct wlr_output *wlr_output = wl_resource_get_user_data(resource);
    return output_from_wlr_output(wlr_output);
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

static bool output_mode_changed(struct output *output, const struct kywc_output_state *state)
{
    struct kywc_output_state *current_state = &output->base.state;
    return current_state->width != state->width || current_state->height != state->height ||
           current_state->refresh != state->refresh;
}

static bool output_gamma_changed(struct output *output, const struct kywc_output_state *state)
{
    struct kywc_output_state *current_state = &output->base.state;
    return current_state->color_temp != state->color_temp ||
           current_state->brightness != state->brightness;
}

static bool output_compare_state(struct output *output, const struct kywc_output_state *state)
{
    struct kywc_output_state *current_state = &output->base.state;

    bool changed = current_state->enabled != state->enabled;
    changed |= current_state->power != state->power;
    changed |= output_mode_changed(output, state);
    changed |= current_state->transform != state->transform;
    changed |= current_state->vrr_policy != state->vrr_policy;
    changed |= current_state->scale != state->scale;

    return changed;
}

bool output_use_hardware_gamma(struct output *output)
{
    return output->base.prop.gamma_size > 0 && !output_manager->force_software_gamma;
}

bool output_state_attempt_gamma(struct output *output, struct wlr_output_state *state)
{
    if (!output->gamma_changed) {
        return false;
    }

    if (!output_use_hardware_gamma(output)) {
        return false;
    }

    uint32_t brightness = output->base.state.brightness;
    uint32_t color_temp = output->base.state.color_temp;

    output_set_gamma_lut(output->wlr_output, output->base.prop.gamma_size, state, color_temp,
                         brightness);
    output->gamma_changed = false;

    return true;
}

bool output_state_attempt_vrr(struct output *output, struct wlr_output_state *state,
                              bool fullscreen)
{
    if (!(output->base.prop.capabilities & KYWC_OUTPUT_CAPABILITY_VRR)) {
        return false;
    }

    if (output->vrr_policy == KYWC_OUTPUT_VRR_POLICY_NEVER ||
        (output->vrr_policy == KYWC_OUTPUT_VRR_POLICY_AUTO && !fullscreen)) {
        if (output->wlr_output->adaptive_sync_status) {
            wlr_output_state_set_adaptive_sync_enabled(state, false);
        }
        return true;
    }

    if (!output->wlr_output->adaptive_sync_status) {
        wlr_output_state_set_adaptive_sync_enabled(state, true);
    }

    return true;
}

bool output_state_attempt_tearing(struct output *output, struct wlr_output_state *state,
                                  bool is_tearing)
{
    if (!is_tearing) {
        return false;
    }

    state->tearing_page_flip = true;
    if (!wlr_output_test_state(output->wlr_output, state)) {
        state->tearing_page_flip = false;
        return false;
    }

    return true;
}

static bool output_set_state(struct output *output, struct kywc_output_state *state)
{
    struct wlr_output *wlr_output = output->wlr_output;
    struct server *server = output_manager->server;

    bool changed = !output->initialized || output_compare_state(output, state);
    bool enabled = state->enabled && state->power;

    if (changed) {
        struct wlr_output_state wlr_state;
        wlr_output_state_init(&wlr_state);
        wlr_output_state_set_enabled(&wlr_state, enabled);

        if (enabled) {
            if (output_mode_changed(output, state)) {
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
            } else if (output->wlr_output->enabled && wlr_output_is_drm(wlr_output) &&
                       output->wlr_output->width == state->width &&
                       output->wlr_output->height == state->height && output->scene_commit &&
                       output->initialized) {
                output->pending_state = *state;
                output->has_pending = true;
                kywc_log(KYWC_WARN, "drm output commit need waiting for pageflip");
                return true;
            }

            wlr_output_state_set_transform(&wlr_state, state->transform);
            wlr_output_state_set_scale(&wlr_state, state->scale);
        }

        output->setting_state = true;
        bool committed = wlr_output_commit_state(wlr_output, &wlr_state);
        output->setting_state = false;
        if (!committed) {
            kywc_log(KYWC_ERROR, "Failed to commit output: %s", wlr_output->name);
            wlr_output_state_finish(&wlr_state);
            return false;
        }
        wlr_output_state_finish(&wlr_state);
    }

    /* fix gamma support by get gamma_size again */
    output->base.prop.gamma_size = wlr_output_get_gamma_size(wlr_output);
    /* gamma settings for brightness and color temperature */
    output->color_temp = state->color_temp;
    output->brightness = state->brightness;

    output->vrr_policy = state->vrr_policy;

    if (enabled && output_gamma_changed(output, state)) {
        output->gamma_changed = true;
        if (output_use_hardware_gamma(output)) {
            output_schedule_frame(output->wlr_output);
        } else {
            // software gamma need update render damage and cursor buffer
            if (output->scene_output) {
                ky_scene_output_damage_whole(output->scene_output);
            }
        }
    }

    /* if output disabled, skip output_layout_add */
    if (!state->enabled) {
        ky_scene_output_destroy(output->scene_output);
        wlr_output_layout_remove(server->layout, wlr_output);
        output->scene_output = NULL;
        return true;
    }

    struct wlr_output_layout_output *loutput = NULL;
    /* force to reconfigure even if have loutput */
    if (state->lx == -1 || state->ly == -1) {
        loutput = wlr_output_layout_add_auto(server->layout, wlr_output);
        if (loutput) {
            loutput->auto_configured = false;
        }
    } else {
        loutput = wlr_output_layout_get(server->layout, wlr_output);
        if (!loutput || loutput->x != state->lx || loutput->y != state->ly) {
            loutput = wlr_output_layout_add(server->layout, wlr_output, state->lx, state->ly);
        }
    }

    if (!output->scene_output && loutput) {
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

static void output_do_update_usable_area(struct output *output, struct kywc_box *usable)
{
    *usable = output->geometry;
    wl_signal_emit_mutable(&output->events.update_usable_area, usable);
    wl_signal_emit_mutable(&output->events.update_late_usable_area, usable);
}

static void output_emit_usable_area(struct output *output)
{
    kywc_log(KYWC_DEBUG, "output %s usable area is (%d, %d) %d x %d", output->base.name,
             output->usable_area.x, output->usable_area.y, output->usable_area.width,
             output->usable_area.height);

    xwayland_update_workarea();

    wl_signal_emit_mutable(&output->events.usable_area, NULL);
}

void output_update_usable_area(struct kywc_output *kywc_output)
{
    /* no need to update usable area when disabled or destroyed */
    if (!kywc_output || kywc_output->destroying || !kywc_output->state.enabled) {
        return;
    }

    struct output *output = output_from_kywc_output(kywc_output);
    struct kywc_box usable_area;

    output_do_update_usable_area(output, &usable_area);
    if (kywc_box_equal(&output->usable_area, &usable_area)) {
        return;
    }

    output->usable_area = usable_area;
    output_emit_usable_area(output);
}

static bool output_manager_is_sorted(void)
{
    int prev_x = 0, prev_y = 0;
    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        int distance_x = output->geometry.x - prev_x;
        int distance_y = output->geometry.y - prev_y;
        if (distance_x < 0 || (distance_x == 0 && distance_y < 0)) {
            return false;
        }
        prev_x = output->geometry.x;
        prev_y = output->geometry.y;
    }
    return true;
}

static void output_manager_sort_outputs(void)
{
    if (output_manager_is_sorted()) {
        return;
    }
    struct output *output, *prev_output, *tmp;
    wl_list_for_each_safe(output, tmp, &output_manager->outputs, link) {
        wl_list_for_each_reverse(prev_output, &output->link, link) {
            if (&prev_output->link == &output_manager->outputs) {
                wl_list_remove(&output->link);
                wl_list_insert(&output_manager->outputs, &output->link);
                break;
            }
            // sort
            int distance_x = output->geometry.x - prev_output->geometry.x;
            int distance_y = output->geometry.y - prev_output->geometry.y;
            if (distance_x < 0 || (distance_x == 0 && distance_y < 0)) {
                continue;
            }
            if (distance_x > 0 || (distance_x == 0 && distance_y > 0)) {
                wl_list_remove(&output->link);
                wl_list_insert(&prev_output->link, &output->link);
                break;
            }
        }
    }
}

static bool output_sync_committed_state(struct output *output, bool enabled)
{
    struct kywc_output *kywc_output = &output->base;
    struct kywc_output_state *current = &kywc_output->state;
    struct kywc_output_state old = kywc_output->state;
    output_get_state(output, current);

    // fix current.enabled for dpms power
    current->enabled = enabled;

    /* update geometry and usable area before all signals */
    bool geometry_changed = false;
    bool usable_area_changed = false;

    if (current->enabled) {
        struct kywc_box geo = output->geometry;
        output_update_geometry(output, &output->geometry);
        geometry_changed = !kywc_box_equal(&geo, &output->geometry);
        /* only update usable area when geometry changed */
        if (!old.enabled || geometry_changed) {
            output_do_update_usable_area(output, &geo);
            if (!kywc_box_equal(&geo, &output->usable_area)) {
                output->usable_area = geo;
                usable_area_changed = true;
            }
        }
    }

    /* sort output manager outputs */
    if (geometry_changed || current->width != old.width || current->height != old.height ||
        current->transform != old.transform || current->scale != old.scale ||
        current->lx != old.lx || current->ly != old.ly) {
        output_manager_sort_outputs();
    }

    output_write_config(output);

    /* early return when not initialized as new_output and new_enabled_output not emitted */
    if (!output->initialized) {
        return true;
    }

    /* check state changes */
    if (current->enabled != old.enabled) {
        if (!current->enabled) {
            wl_signal_emit_mutable(&kywc_output->events.off, NULL);
            wl_signal_emit_mutable(&output->events.disable, NULL);
            return true;
        } else {
            wl_signal_emit_mutable(&kywc_output->events.on, NULL);
            wl_signal_emit_mutable(&output_manager->events.new_enabled_output, kywc_output);
        }
    }

    if (geometry_changed) {
        kywc_log(KYWC_DEBUG, "output %s geometry is (%d, %d) %d x %d", output->base.name,
                 output->geometry.x, output->geometry.y, output->geometry.width,
                 output->geometry.height);
        wl_signal_emit_mutable(&output->events.geometry, NULL);
    }

    if (usable_area_changed) {
        output_emit_usable_area(output);
    }

    if (kywc_output->prop.capabilities & KYWC_OUTPUT_CAPABILITY_POWER &&
        current->power != old.power) {
        wl_signal_emit_mutable(&kywc_output->events.power, NULL);
    }

    if (kywc_output->prop.capabilities & KYWC_OUTPUT_CAPABILITY_BRIGHTNESS &&
        current->brightness != old.brightness) {
        wl_signal_emit_mutable(&kywc_output->events.brightness, NULL);
    }

    if (kywc_output->prop.capabilities & KYWC_OUTPUT_CAPABILITY_COLOR_TEMP &&
        current->color_temp != old.color_temp) {
        wl_signal_emit_mutable(&kywc_output->events.color_temp, NULL);
    }

    if (current->width != old.width || current->height != old.height ||
        current->refresh != old.refresh) {
        wl_signal_emit_mutable(&kywc_output->events.mode, NULL);
    }

    if (current->transform != old.transform) {
        output_add_transform_effect(kywc_output, &old, current);
        wl_signal_emit_mutable(&kywc_output->events.transform, NULL);
    }

    if (current->scale != old.scale) {
        wl_signal_emit_mutable(&kywc_output->events.scale, NULL);
    }

    if (current->lx != old.lx || current->ly != old.ly) {
        wl_signal_emit_mutable(&kywc_output->events.position, NULL);
    }

    return true;
}

bool kywc_output_set_state(struct kywc_output *kywc_output, struct kywc_output_state *state)
{
    struct output *output = output_from_kywc_output(kywc_output);
    if (!output_set_state(output, state)) {
        return false;
    }

    return output_sync_committed_state(output, state->enabled);
}

struct kywc_output *kywc_output_by_name(const char *name)
{
    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (strcmp(name, output->base.name) == 0) {
            return &output->base;
        }
    }

    return NULL;
}

struct kywc_output *kywc_output_by_uuid(const char *uuid)
{
    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (strcmp(uuid, output->base.uuid) == 0) {
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

static struct output *output_get_prve_usable_output(struct output *output)
{
    struct output *find_output = NULL;
    wl_list_for_each_reverse(find_output, &output->link, link) {
        if (&find_output->link == &output_manager->outputs) {
            continue;
        }
        if (find_output->base.state.enabled) {
            return find_output;
        }
    }
    return NULL;
}

static struct output *output_get_next_usable_output(struct output *output)
{
    struct output *find_output = NULL;
    wl_list_for_each(find_output, &output->link, link) {
        if (&find_output->link == &output_manager->outputs) {
            continue;
        }
        if (find_output->base.state.enabled) {
            return find_output;
        }
    }
    return NULL;
}

struct output *output_find_specified_output(struct output *output, enum layout_edge edge)
{
    struct output *find_output = NULL;
    switch (edge) {
    case LAYOUT_EDGE_LEFT:
        find_output = output_get_prve_usable_output(output);
        break;
    case LAYOUT_EDGE_RIGHT:
        find_output = output_get_next_usable_output(output);
        break;
    case LAYOUT_EDGE_TOP:
    case LAYOUT_EDGE_BOTTOM:
        break;
    }
    return find_output;
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

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

void output_layout_get_size(int *width, int *height)
{
    struct wlr_box box;
    wlr_output_layout_get_box(output_manager->server->layout, NULL, &box);

    if (width) {
        *width = box.width;
    }
    if (height) {
        *height = box.height;
    }
}

void output_layout_get_workarea(struct wlr_box *box)
{
    wlr_output_layout_get_box(output_manager->server->layout, NULL, box);

    /* layout edges */
    int layout_left = MAX(0, box->x);
    int layout_right = MAX(0, box->x + box->width);
    int layout_top = MAX(0, box->y);
    int layout_bottom = MAX(0, box->y + box->height);

    /* workarea edges */
    int workarea_left = layout_left;
    int workarea_right = layout_right;
    int workarea_top = layout_top;
    int workarea_bottom = layout_bottom;

    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (!output->base.state.enabled) {
            continue;
        }

        /* output edges */
        int output_left = output->geometry.x;
        int output_right = output_left + output->geometry.width;
        int output_top = output->geometry.y;
        int output_bottom = output_top + output->geometry.height;

        /* output usable edges */
        int usable_left = output->usable_area.x;
        int usable_right = usable_left + output->usable_area.width;
        int usable_top = output->usable_area.y;
        int usable_bottom = usable_top + output->usable_area.height;

        /*
         * Only adjust workarea edges for output edges that are
         * aligned with outer edges of layout
         */
        if (output_left == layout_left) {
            workarea_left = MAX(workarea_left, usable_left);
        }
        if (output_right == layout_right) {
            workarea_right = MIN(workarea_right, usable_right);
        }
        if (output_top == layout_top) {
            workarea_top = MAX(workarea_top, usable_top);
        }
        if (output_bottom == layout_bottom) {
            workarea_bottom = MIN(workarea_bottom, usable_bottom);
        }
    }

    box->x = workarea_left;
    box->y = workarea_top;
    box->width = workarea_right - workarea_left;
    box->height = workarea_bottom - workarea_top;
}

void output_schedule_frame(struct wlr_output *wlr_output)
{
    if (output_manager->server->active) {
        wlr_output_schedule_frame(wlr_output);
    }
}
