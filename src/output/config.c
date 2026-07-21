// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <math.h>

#include "config.h"
#include "output_p.h"
#include "util/dbus.h"

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

static int apply_primary_state(sd_bus_message *m, struct kywc_output_state *state)
{
    struct kywc_output *output = kywc_output_get_primary();
    if (!output || output->prop.is_virtual || output->prop.is_fbdev) {
        sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(
            SD_BUS_ERROR_FAILED, "No configurable primary screen is available.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (!kywc_output_set_state(output, state)) {
        sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(
            SD_BUS_ERROR_FAILED, "The requested screen configuration is not supported.");
        return sd_bus_reply_method_error(m, &error);
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

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListAllOutputs", "", "a(ss)", list_outputs, 0),
    SD_BUS_METHOD("SetBrightness", "su", "", set_brightness, 0),
    SD_BUS_METHOD("SetColortemp", "su", "", set_colortemp, 0),
    SD_BUS_VTABLE_END,
};

static const sd_bus_vtable gxde_screen_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("SetScaleRatio", "d", "", set_scale_ratio, 0),
    SD_BUS_METHOD("SetResolutionWRefreshRate", "iii", "", set_resolution_with_refresh_rate, 0),
    SD_BUS_METHOD("SetScreenBrightness", "si", "", set_screen_brightness, 0),
    SD_BUS_METHOD("RotateScreen", "i", "", rotate_screen, 0),
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
