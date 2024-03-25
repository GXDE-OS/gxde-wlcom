// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include "config.h"
#include "output_p.h"

static const char *service_path = "/com/kylin/Wlcom/Output";
static const char *service_interface = "com.kylin.Wlcom.Output";

static int list_outputs(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct output_manager *om = userdata;

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(ss)"));

    struct output *output;
    wl_list_for_each(output, &om->outputs, link) {
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
        output_set_brightness(kywc_output, value);
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
        output_set_colortemp(kywc_output, value);
    }
    return sd_bus_reply_method_return(m, NULL);
}

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListAllOutputs", "", "a(ss)", list_outputs, 0),
    SD_BUS_METHOD("SetBrightness", "su", "", set_brightness, 0),
    SD_BUS_METHOD("SetColortemp", "su", "", set_colortemp, 0),
    SD_BUS_VTABLE_END,
};

bool output_manager_config_init(struct output_manager *output_manager)
{
    output_manager->config = config_manager_add_config(
        "outputs", NULL, service_path, service_interface, service_vtable, output_manager);
    return !!output_manager->config;
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
    if (!om->config || !om->config->json || output->base.prop.is_virtual) {
        return;
    }

    struct kywc_output_state *state = &output->base.state;
    /* get output in layout, create if no */
    json_object *config = json_object_object_get(om->config->json, output->base.name);
    if (!config) {
        config = json_object_new_object();
        json_object_object_add(om->config->json, output->base.name, config);
    }

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
