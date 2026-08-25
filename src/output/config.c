// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <math.h>

#include "config.h"
#include "input/input.h"
#include "output_p.h"
#include "util/dbus.h"

enum gxde_screen_mode {
    GXDE_SCREEN_MODE_DUPLICATE = 0,
    GXDE_SCREEN_MODE_EXTEND,
    GXDE_SCREEN_MODE_SINGLE,
};

struct screen_layout {
    struct output *output;
    int32_t x, y;
};

static const char *service_path = "/com/kylin/Wlcom/Output";
static const char *service_interface = "com.kylin.Wlcom.Output";
static const char *gxde_screen_service = "top.gxde.Wlcom.Screen";
static const char *gxde_screen_path = "/top/gxde/Wlcom/Screen";
static const char *gxde_screen_interface = "top.gxde.Wlcom.Screen";

static int reply_invalid_args(sd_bus_message *m, const char *message)
{
    sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, message);
    return sd_bus_reply_method_error(m, &error);
}

static int reply_failed(sd_bus_message *m, const char *message)
{
    sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_FAILED, message);
    return sd_bus_reply_method_error(m, &error);
}

static bool output_is_configurable(const struct kywc_output *output)
{
    return output && !output->prop.is_virtual && !output->prop.is_fbdev;
}

static uint32_t configurable_output_count(struct output_manager *manager)
{
    uint32_t count = 0;
    struct output *output;
    wl_list_for_each(output, &manager->outputs, link) {
        if (output_is_configurable(&output->base)) {
            count++;
        }
    }
    return count;
}

static struct kywc_output_mode *find_output_mode(struct kywc_output *output, int32_t width,
                                                 int32_t height)
{
    struct kywc_output_mode *best = NULL;
    struct kywc_output_mode *mode;
    wl_list_for_each(mode, &output->prop.modes, link) {
        if (mode->width != width || mode->height != height) {
            continue;
        }

        if (!best || mode->refresh > best->refresh) {
            best = mode;
        }
    }
    return best;
}

static bool mode_supported_by_all_outputs(struct output_manager *manager, int32_t width,
                                          int32_t height)
{
    struct output *output;
    wl_list_for_each(output, &manager->outputs, link) {
        if (!output_is_configurable(&output->base)) {
            continue;
        }
        if (!find_output_mode(&output->base, width, height)) {
            return false;
        }
    }
    return true;
}

static bool find_largest_common_mode(struct output_manager *manager, int32_t *width,
                                     int32_t *height)
{
    int64_t best_area = 0;
    struct output *output;
    wl_list_for_each(output, &manager->outputs, link) {
        if (!output_is_configurable(&output->base)) {
            continue;
        }

        struct kywc_output_mode *mode;
        wl_list_for_each(mode, &output->base.prop.modes, link) {
            int64_t area = (int64_t)mode->width * mode->height;
            if (area <= best_area ||
                !mode_supported_by_all_outputs(manager, mode->width, mode->height)) {
                continue;
            }
            best_area = area;
            *width = mode->width;
            *height = mode->height;
        }
        break;
    }
    return best_area > 0;
}

static bool prepare_enabled_state(struct kywc_output *output, struct kywc_output_state *state)
{
    *state = output->state;
    state->enabled = state->power = true;

    if (state->width > 0 && state->height > 0 &&
        find_output_mode(output, state->width, state->height)) {
        if (!isfinite(state->scale) || state->scale <= 0.0f) {
            state->scale = kywc_output_preferred_scale(output, state->width, state->height);
        }
        return true;
    }

    if (wl_list_empty(&output->prop.modes)) {
        return false;
    }

    struct kywc_output_mode *mode = kywc_output_preferred_mode(output);
    state->width = mode->width;
    state->height = mode->height;
    state->refresh = mode->refresh;
    if (!isfinite(state->scale) || state->scale <= 0.0f) {
        state->scale = kywc_output_preferred_scale(output, state->width, state->height);
    }
    return true;
}

static int32_t output_state_effective_width(const struct kywc_output_state *state)
{
    int32_t width = state->transform % 2 == 0 ? state->width : state->height;
    float scale = isfinite(state->scale) && state->scale > 0.0f ? state->scale : 1.0f;
    return width / scale;
}

static int apply_output_configuration(sd_bus_message *m)
{
    if (!output_manager_configure_outputs()) {
        return reply_failed(m, "The requested screen configuration is not supported.");
    }

    config_manager_sync();
    return sd_bus_reply_method_return(m, NULL);
}

static int get_cursor_output(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct seat *seat = input_manager_get_default_seat();
    struct output *output = seat ? input_current_output(seat) : NULL;
    return sd_bus_reply_method_return(m, "s", output ? output->base.name : "");
}

static int apply_output_state(sd_bus_message *m, struct kywc_output *output,
                              struct kywc_output_state *state)
{
    if (!output_is_configurable(output)) {
        return reply_invalid_args(m, "The requested screen was not found.");
    }
    if (!kywc_output_set_state(output, state)) {
        return reply_failed(m, "The requested screen configuration is not supported.");
    }

    output_manager_emit_configured(CONFIGURE_TYPE_UPDATE);
    config_manager_sync();
    return sd_bus_reply_method_return(m, NULL);
}

static int apply_primary_state(sd_bus_message *m, struct kywc_output_state *state)
{
    struct kywc_output *output = kywc_output_get_primary();
    if (!output_is_configurable(output)) {
        return reply_failed(m, "No configurable primary screen is available.");
    }

    if (!kywc_output_set_state(output, state)) {
        return reply_failed(m, "The requested screen configuration is not supported.");
    }

    output_manager_emit_configured(CONFIGURE_TYPE_UPDATE);
    config_manager_sync();
    return sd_bus_reply_method_return(m, NULL);
}

static int list_outputs(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct output_manager *om = userdata;

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(ss)"));

    struct output *output;
    wl_list_for_each(output, &om->outputs, link) {
        if (output->base.prop.is_virtual || output->base.prop.is_fbdev) {
            continue;
        }
        output_write_config(output);
        json_object *config = json_object_object_get(om->config->json, output->base.name);
        const char *cfg = json_object_to_json_string(config);
        sd_bus_message_append(reply, "(ss)", output->base.name, cfg);
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static int set_brightness(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    char *name = NULL;
    uint32_t value = 0;
    CK(sd_bus_message_read(m, "su", &name, &value));

    struct kywc_output *kywc_output = kywc_output_by_name(name);
    if (kywc_output) {
        if (output_set_brightness(kywc_output, value)) {
            output_manager_emit_configured(CONFIGURE_TYPE_REMAIN);
        }
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int set_colortemp(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    char *name = NULL;
    uint32_t value = 0;
    CK(sd_bus_message_read(m, "su", &name, &value));

    struct kywc_output *kywc_output = kywc_output_by_name(name);
    if (kywc_output) {
        if (output_set_colortemp(kywc_output, value)) {
            output_manager_emit_configured(CONFIGURE_TYPE_REMAIN);
        }
    }
    return sd_bus_reply_method_return(m, NULL);
}

static int set_scale_ratio(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    double ratio = 0;
    CK(sd_bus_message_read(m, "d", &ratio));
    if (!isfinite(ratio) || ratio < 1.0 || ratio > 3.0) {
        return reply_invalid_args(m, "Scale ratio must be between 1.0 and 3.0.");
    }

    struct kywc_output *output = kywc_output_get_primary();
    if (!output) {
        return apply_primary_state(m, NULL);
    }
    struct kywc_output_state state = output->state;
    state.scale = (float)((int)(ratio * 100.0 + 0.5) / 100.0);
    return apply_primary_state(m, &state);
}

static int set_resolution_with_refresh_rate(sd_bus_message *m, void *userdata,
                                            sd_bus_error *ret_error)
{
    int32_t width = 0, height = 0, refresh = 0;
    CK(sd_bus_message_read(m, "iii", &width, &height, &refresh));
    if (width <= 0 || height <= 0 || refresh <= 0 || refresh > INT32_MAX / 1000) {
        return reply_invalid_args(m, "Width, height and refresh rate must be positive integers.");
    }

    struct kywc_output *output = kywc_output_get_primary();
    if (!output) {
        return apply_primary_state(m, NULL);
    }
    struct kywc_output_state state = output->state;
    state.width = width;
    state.height = height;
    state.refresh = refresh * 1000;
    return apply_primary_state(m, &state);
}

static int set_screen_brightness(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    int32_t brightness = 0;
    CK(sd_bus_message_read(m, "si", &name, &brightness));
    if (brightness < 0 || brightness > 100) {
        return reply_invalid_args(m, "Brightness must be between 0 and 100.");
    }

    struct kywc_output *output = kywc_output_by_name(name);
    if (!output) {
        sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Screen was not found.");
        return sd_bus_reply_method_error(m, &error);
    }
    if (!output_set_brightness(output, (uint32_t)brightness)) {
        sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(
            SD_BUS_ERROR_FAILED, "The screen does not support the requested brightness.");
        return sd_bus_reply_method_error(m, &error);
    }

    output_manager_emit_configured(CONFIGURE_TYPE_REMAIN);
    return sd_bus_reply_method_return(m, NULL);
}

static int rotate_screen(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    int32_t angle = 0;
    CK(sd_bus_message_read(m, "i", &angle));
    if (angle != 0 && angle != 90 && angle != 180 && angle != 270) {
        return reply_invalid_args(m, "Rotation must be one of 0, 90, 180 or 270 degrees.");
    }

    struct kywc_output *output = kywc_output_get_primary();
    if (!output) {
        return apply_primary_state(m, NULL);
    }
    struct kywc_output_state state = output->state;
    state.transform = angle / 90;
    return apply_primary_state(m, &state);
}

static int set_screen_scale(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    double ratio = 0;
    CK(sd_bus_message_read(m, "sd", &name, &ratio));
    if (!isfinite(ratio) || ratio < 1.0 || ratio > 3.0) {
        return reply_invalid_args(m, "Scale ratio must be between 1.0 and 3.0.");
    }

    struct kywc_output *output = kywc_output_by_name(name);
    if (!output_is_configurable(output)) {
        return reply_invalid_args(m, "The requested screen was not found.");
    }
    struct kywc_output_state state = output->state;
    state.scale = (float)((int)(ratio * 100.0 + 0.5) / 100.0);
    return apply_output_state(m, output, &state);
}

static int set_screen_resolution(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    int32_t width = 0, height = 0, refresh = 0;
    CK(sd_bus_message_read(m, "siii", &name, &width, &height, &refresh));
    if (width <= 0 || height <= 0 || refresh <= 0 || refresh > INT32_MAX / 1000) {
        return reply_invalid_args(m, "Width, height and refresh rate must be positive integers.");
    }

    struct kywc_output *output = kywc_output_by_name(name);
    if (!output_is_configurable(output)) {
        return reply_invalid_args(m, "The requested screen was not found.");
    }
    struct kywc_output_state state = output->state;
    state.width = width;
    state.height = height;
    state.refresh = refresh * 1000;
    return apply_output_state(m, output, &state);
}

static int set_screen_rotation(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    int32_t angle = 0;
    CK(sd_bus_message_read(m, "si", &name, &angle));
    if (angle != 0 && angle != 90 && angle != 180 && angle != 270) {
        return reply_invalid_args(m, "Rotation must be one of 0, 90, 180 or 270 degrees.");
    }

    struct kywc_output *output = kywc_output_by_name(name);
    if (!output_is_configurable(output)) {
        return reply_invalid_args(m, "The requested screen was not found.");
    }
    struct kywc_output_state state = output->state;
    state.transform = angle / 90;
    return apply_output_state(m, output, &state);
}

static int set_screen_enabled(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    int enabled = 0;
    CK(sd_bus_message_read(m, "sb", &name, &enabled));

    struct kywc_output *output = kywc_output_by_name(name);
    if (!output_is_configurable(output)) {
        return reply_invalid_args(m, "The requested screen was not found.");
    }

    struct kywc_output_state state = output->state;
    if (enabled) {
        if (!prepare_enabled_state(output, &state)) {
            return reply_failed(m, "The screen does not provide any usable display mode.");
        }
    } else {
        state.enabled = state.power = false;
    }
    output_manager_add_output_pending_state(output_from_kywc_output(output), &state);
    return apply_output_configuration(m);
}

static int set_primary_screen(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    CK(sd_bus_message_read(m, "s", &name));

    struct kywc_output *output = kywc_output_by_name(name);
    if (!output_is_configurable(output) || !output->state.enabled) {
        return reply_invalid_args(m, "The requested screen was not found or is disabled.");
    }

    struct output *selected = output_from_kywc_output(output);
    output_manager_add_output_pending_state(selected, &output->state);
    output_set_pending_primary(selected);
    return apply_output_configuration(m);
}

static int set_screen_position(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    int32_t x = 0, y = 0;
    CK(sd_bus_message_read(m, "sii", &name, &x, &y));

    struct kywc_output *output = kywc_output_by_name(name);
    if (!output_is_configurable(output) || !output->state.enabled) {
        return reply_invalid_args(m, "The requested screen was not found or is disabled.");
    }

    struct kywc_output_state state = output->state;
    state.lx = x;
    state.ly = y;
    output_manager_add_output_pending_state(output_from_kywc_output(output), &state);
    return apply_output_configuration(m);
}

static int set_duplicate_mode(sd_bus_message *m, struct output_manager *manager)
{
    if (configurable_output_count(manager) < 2) {
        return reply_failed(m, "Duplicate mode requires at least two screens.");
    }

    int32_t width = 0, height = 0;
    if (!find_largest_common_mode(manager, &width, &height)) {
        return reply_failed(m, "The screens do not have a common resolution.");
    }

    struct kywc_output *reference = kywc_output_get_primary();
    if (!output_is_configurable(reference)) {
        reference = NULL;
    }

    struct output *output;
    wl_list_for_each(output, &manager->outputs, link) {
        if (!output_is_configurable(&output->base)) {
            continue;
        }
        if (!reference) {
            reference = &output->base;
        }
    }

    float scale = reference->state.scale;
    if (!isfinite(scale) || scale <= 0.0f) {
        scale = kywc_output_preferred_scale(reference, width, height);
    }

    wl_list_for_each(output, &manager->outputs, link) {
        struct kywc_output *kywc_output = &output->base;
        if (!output_is_configurable(kywc_output)) {
            continue;
        }

        struct kywc_output_state state;
        if (!prepare_enabled_state(kywc_output, &state)) {
            return reply_failed(m, "A screen does not provide any usable display mode.");
        }
        struct kywc_output_mode *mode = find_output_mode(kywc_output, width, height);
        state.width = width;
        state.height = height;
        state.refresh = mode->refresh;
        state.scale = scale;
        state.transform = reference->state.transform;
        state.lx = state.ly = 0;
        output_manager_add_output_pending_state(output, &state);
    }

    output_set_pending_primary(output_from_kywc_output(reference));
    return apply_output_configuration(m);
}

static int set_extend_mode(sd_bus_message *m, struct output_manager *manager)
{
    struct output *output;
    wl_list_for_each(output, &manager->outputs, link) {
        if (output_is_configurable(&output->base) && wl_list_empty(&output->base.prop.modes)) {
            return reply_failed(m, "A screen does not provide any usable display mode.");
        }
    }

    int32_t x = 0;
    struct output *first = NULL;
    wl_list_for_each(output, &manager->outputs, link) {
        struct kywc_output *kywc_output = &output->base;
        if (!output_is_configurable(kywc_output)) {
            continue;
        }

        struct kywc_output_state state;
        if (!prepare_enabled_state(kywc_output, &state)) {
            return reply_failed(m, "A screen does not provide any usable display mode.");
        }
        if (!first) {
            first = output;
        }
        state.lx = x;
        state.ly = 0;
        output_manager_add_output_pending_state(output, &state);
        x += output_state_effective_width(&state);
    }

    if (!first) {
        return reply_failed(m, "No configurable screen is available.");
    }
    struct kywc_output *primary = kywc_output_get_primary();
    output_set_pending_primary(output_is_configurable(primary) ? output_from_kywc_output(primary)
                                                               : first);
    return apply_output_configuration(m);
}

static int set_single_mode(sd_bus_message *m, struct output_manager *manager, const char *name)
{
    struct kywc_output *selected = kywc_output_by_name(name);
    if (!output_is_configurable(selected)) {
        return reply_invalid_args(m, "The requested screen was not found.");
    }

    struct kywc_output_state selected_state;
    if (!prepare_enabled_state(selected, &selected_state)) {
        return reply_failed(m, "The screen does not provide any usable display mode.");
    }
    selected_state.lx = selected_state.ly = 0;

    struct output *output;
    wl_list_for_each(output, &manager->outputs, link) {
        struct kywc_output *kywc_output = &output->base;
        if (!output_is_configurable(kywc_output)) {
            continue;
        }

        struct kywc_output_state state = kywc_output->state;
        if (kywc_output == selected) {
            state = selected_state;
        } else {
            state.enabled = state.power = false;
        }
        output_manager_add_output_pending_state(output, &state);
    }

    output_set_pending_primary(output_from_kywc_output(selected));
    return apply_output_configuration(m);
}

static int set_screen_mode(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct output_manager *manager = userdata;
    uint32_t mode = 0;
    const char *screen = NULL;
    CK(sd_bus_message_read(m, "us", &mode, &screen));

    switch (mode) {
    case GXDE_SCREEN_MODE_DUPLICATE:
        return set_duplicate_mode(m, manager);
    case GXDE_SCREEN_MODE_EXTEND:
        return set_extend_mode(m, manager);
    case GXDE_SCREEN_MODE_SINGLE:
        if (!screen || !screen[0]) {
            return reply_invalid_args(m, "Single-screen mode requires a screen name.");
        }
        return set_single_mode(m, manager, screen);
    default:
        return reply_invalid_args(m,
                                  "Screen mode must be 0 (duplicate), 1 (extend) or 2 (single).");
    }
}

static int set_screen_layout(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct output_manager *manager = userdata;
    uint32_t enabled_count = 0;
    struct output *output;
    wl_list_for_each(output, &manager->outputs, link) {
        if (output_is_configurable(&output->base) && output->base.state.enabled) {
            enabled_count++;
        }
    }
    if (!enabled_count) {
        return reply_failed(m, "No enabled configurable screen is available.");
    }

    struct screen_layout *layouts = calloc(enabled_count, sizeof(*layouts));
    if (!layouts) {
        return -ENOMEM;
    }

    int ret = sd_bus_message_enter_container(m, 'a', "(sii)");
    if (ret < 0) {
        free(layouts);
        return ret;
    }

    uint32_t count = 0;
    const char *name = NULL;
    int32_t x = 0, y = 0;
    while ((ret = sd_bus_message_read(m, "(sii)", &name, &x, &y)) > 0) {
        struct kywc_output *kywc_output = kywc_output_by_name(name);
        if (!output_is_configurable(kywc_output) || !kywc_output->state.enabled) {
            free(layouts);
            return reply_invalid_args(m, "The layout contains an unknown or disabled screen.");
        }
        if (count >= enabled_count) {
            free(layouts);
            return reply_invalid_args(m, "The layout contains too many screens.");
        }

        struct output *item = output_from_kywc_output(kywc_output);
        for (uint32_t i = 0; i < count; ++i) {
            if (layouts[i].output == item) {
                free(layouts);
                return reply_invalid_args(m, "Each screen may only occur once in the layout.");
            }
        }
        layouts[count++] = (struct screen_layout){
            .output = item,
            .x = x,
            .y = y,
        };
    }
    if (ret < 0) {
        free(layouts);
        return ret;
    }
    ret = sd_bus_message_exit_container(m);
    if (ret < 0) {
        free(layouts);
        return ret;
    }
    if (count != enabled_count) {
        free(layouts);
        return reply_invalid_args(m, "The layout must contain every enabled screen exactly once.");
    }

    for (uint32_t i = 0; i < count; ++i) {
        struct kywc_output_state state = layouts[i].output->base.state;
        state.lx = layouts[i].x;
        state.ly = layouts[i].y;
        output_manager_add_output_pending_state(layouts[i].output, &state);
    }
    free(layouts);

    struct kywc_output *primary = kywc_output_get_primary();
    if (output_is_configurable(primary) && primary->state.enabled) {
        output_set_pending_primary(output_from_kywc_output(primary));
    }
    return apply_output_configuration(m);
}

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListAllOutputs", "", "a(ss)", list_outputs, 0),
    SD_BUS_METHOD("SetBrightness", "su", "", set_brightness, 0),
    SD_BUS_METHOD("SetColortemp", "su", "", set_colortemp, 0),
    SD_BUS_VTABLE_END,
};

static const sd_bus_vtable gxde_screen_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetCursorOutput", "", "s", get_cursor_output,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetScaleRatio", "d", "", set_scale_ratio, 0),
    SD_BUS_METHOD("SetResolutionWRefreshRate", "iii", "", set_resolution_with_refresh_rate, 0),
    SD_BUS_METHOD("SetScreenBrightness", "si", "", set_screen_brightness, 0),
    SD_BUS_METHOD("RotateScreen", "i", "", rotate_screen, 0),
    SD_BUS_METHOD("SetScreenScale", "sd", "", set_screen_scale, 0),
    SD_BUS_METHOD("SetScreenResolution", "siii", "", set_screen_resolution, 0),
    SD_BUS_METHOD("SetScreenRotation", "si", "", set_screen_rotation, 0),
    SD_BUS_METHOD("SetScreenEnabled", "sb", "", set_screen_enabled, 0),
    SD_BUS_METHOD("SetPrimaryScreen", "s", "", set_primary_screen, 0),
    SD_BUS_METHOD("SetScreenPosition", "sii", "", set_screen_position, 0),
    SD_BUS_METHOD("SetScreenMode", "us", "", set_screen_mode, 0),
    SD_BUS_METHOD("SetScreenLayout", "a(sii)", "", set_screen_layout, 0),
    SD_BUS_VTABLE_END,
};

bool output_manager_config_init(struct output_manager *output_manager)
{
    output_manager->config = config_manager_add_config("outputs");
    if (!output_manager->config) {
        return false;
    }
    if (!dbus_register_object(NULL, service_path, service_interface, service_vtable,
                              output_manager)) {
        return false;
    }
    return dbus_register_object(gxde_screen_service, gxde_screen_path, gxde_screen_interface,
                                gxde_screen_vtable, output_manager);
}

bool output_read_config(struct output *output, struct kywc_output_state *state)
{
    struct output_manager *om = output->manager;
    if (!om->config || !om->config->json) {
        return false;
    }

    /* get output in layout */
    json_object *config = json_object_object_get(om->config->json, output->base.name);
    if (!config) {
        return false;
    }

    /* finally, get all output config */
    json_object *data;

    if (!json_object_object_get_ex(config, "uuid", &data)) {
        return false;
    }

    const char *uuid = json_object_get_string(data);
    if (!uuid || strcmp(uuid, output->base.uuid)) {
        return false;
    }

    if (json_object_object_get_ex(config, "enabled", &data)) {
        state->power = state->enabled = json_object_get_boolean(data);
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
    if (json_object_object_get_ex(config, "brightness", &data)) {
        state->brightness = json_object_get_int(data);
    }
    if (json_object_object_get_ex(config, "color_temp", &data)) {
        state->color_temp = json_object_get_int(data);
    }
    if (json_object_object_get_ex(config, "primary", &data)) {
        state->primary = json_object_get_boolean(data);
    }

    return true;
}

void output_write_config(struct output *output)
{
    struct output_manager *om = output->manager;
    if (!om->config || !om->config->json || output->base.prop.is_virtual ||
        output->base.prop.is_fbdev) {
        return;
    }

    struct kywc_output_state *state = &output->base.state;
    /* get output in layout, create if no */
    json_object *config = json_object_object_get(om->config->json, output->base.name);
    if (!config) {
        config = json_object_new_object();
        json_object_object_add(om->config->json, output->base.name, config);
    }

    json_object_object_add(config, "uuid", json_object_new_string(output->base.uuid));
    json_object_object_add(config, "enabled", json_object_new_boolean(state->enabled));
    json_object_object_add(config, "width", json_object_new_int(state->width));
    json_object_object_add(config, "height", json_object_new_int(state->height));
    json_object_object_add(config, "refresh", json_object_new_int(state->refresh));
    json_object_object_add(config, "scale", json_object_new_double(state->scale));
    json_object_object_add(config, "transform", json_object_new_int(state->transform));
    json_object_object_add(config, "lx", json_object_new_int(state->lx));
    json_object_object_add(config, "ly", json_object_new_int(state->ly));
    json_object_object_add(config, "brightness", json_object_new_int(state->brightness));
    json_object_object_add(config, "color_temp", json_object_new_int(state->color_temp));
    json_object_object_add(config, "primary", json_object_new_boolean(state->primary));
}
