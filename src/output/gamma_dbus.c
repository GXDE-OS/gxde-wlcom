// SPDX-FileCopyrightText: 2026 CharOfString <root@charofstring.cc>
//
// SPDX-License-Identifier: GPL-3.0-or-later

/* Originally implemented in FlakeWM, ported here. */

#include "config.h"
#include "output_p.h"
#include "util/dbus.h"

static const char *gamma_service = "top.gxde.Wlcom.Gamma";
static const char *gamma_path = "/top/gxde/Wlcom/Gamma";
static const char *gamma_interface = "top.gxde.Wlcom.Gamma";

#define GAMMA_ERROR_NOT_FOUND "top.gxde.Wlcom.Gamma.Error.NotFound"
#define GAMMA_ERROR_NOT_SUPPORTED "top.gxde.Wlcom.Gamma.Error.NotSupported"

static int reply_error(sd_bus_message *m, const char *name, const char *message)
{
    sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(name, message);
    return sd_bus_reply_method_error(m, &error);
}

static bool output_has_gamma(struct output *output)
{
    struct kywc_output *kywc_output = &output->base;
    return !kywc_output->destroying && !kywc_output->prop.is_virtual &&
           !kywc_output->prop.is_fbdev;
}

static bool output_gamma_supported(struct output *output)
{
    return output_has_gamma(output) && output_use_hardware_gamma(output);
}

static struct output *find_output(const char *name)
{
    struct kywc_output *kywc_output = kywc_output_by_name(name);
    if (!kywc_output) {
        return NULL;
    }
    struct output *output = output_from_kywc_output(kywc_output);
    return output_has_gamma(output) ? output : NULL;
}

static void emit_gamma_changed(struct output *output)
{
    dbus_emit_signal(gamma_path, gamma_interface, "GammaChanged", "sddd", output->base.name,
                     (double)output->gamma[0], (double)output->gamma[1],
                     (double)output->gamma[2]);
}

static int list_outputs(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct output_manager *om = userdata;

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));

    int ret = sd_bus_message_open_container(reply, 'a', "(sbbddd)");
    struct output *output;
    wl_list_for_each(output, &om->outputs, link) {
        if (ret < 0) {
            break;
        }
        if (!output_has_gamma(output)) {
            continue;
        }
        bool client_active = output_get_gamma_control(output->wlr_output) != NULL;
        ret = sd_bus_message_append(reply, "(sbbddd)", output->base.name,
                                    output_gamma_supported(output), client_active,
                                    (double)output->gamma[0], (double)output->gamma[1],
                                    (double)output->gamma[2]);
    }
    if (ret >= 0) {
        ret = sd_bus_message_close_container(reply);
    }
    if (ret >= 0) {
        ret = sd_bus_send(NULL, reply, NULL);
    }
    sd_bus_message_unref(reply);
    return ret < 0 ? ret : 1;
}

static int get_gamma(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    CK(sd_bus_message_read(m, "s", &name));

    struct output *output = find_output(name);
    if (!output) {
        return reply_error(m, GAMMA_ERROR_NOT_FOUND, "Screen was not found.");
    }

    return sd_bus_reply_method_return(m, "ddd", (double)output->gamma[0],
                                      (double)output->gamma[1], (double)output->gamma[2]);
}

static int apply_gamma(sd_bus_message *m, struct output_manager *om, const char *name,
                       const float gamma[3])
{
    if (name && *name) {
        struct output *output = find_output(name);
        if (!output) {
            return reply_error(m, GAMMA_ERROR_NOT_FOUND, "Screen was not found.");
        }
        if (!output_gamma_supported(output)) {
            return reply_error(m, GAMMA_ERROR_NOT_SUPPORTED,
                               "The screen does not support hardware gamma.");
        }
        if (!output_set_gamma(output, gamma)) {
            return reply_error(m, SD_BUS_ERROR_FAILED, "Failed to set gamma.");
        }
        emit_gamma_changed(output);
        config_manager_sync();
        return sd_bus_reply_method_return(m, NULL);
    }

    /* empty name: every output that supports gamma */
    uint32_t count = 0;
    struct output *output;
    wl_list_for_each(output, &om->outputs, link) {
        if (!output_gamma_supported(output) || !output_set_gamma(output, gamma)) {
            continue;
        }
        emit_gamma_changed(output);
        count++;
    }
    if (count == 0) {
        return reply_error(m, GAMMA_ERROR_NOT_SUPPORTED, "No screen supports hardware gamma.");
    }
    config_manager_sync();
    return sd_bus_reply_method_return(m, NULL);
}

static int set_gamma(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    double red = 0, green = 0, blue = 0;
    CK(sd_bus_message_read(m, "sddd", &name, &red, &green, &blue));

    if (!output_gamma_is_valid(red) || !output_gamma_is_valid(green) ||
        !output_gamma_is_valid(blue)) {
        return reply_error(m, SD_BUS_ERROR_INVALID_ARGS,
                           "Gamma must be a number between 0.1 and 10.0.");
    }

    const float gamma[3] = { red, green, blue };
    return apply_gamma(m, userdata, name, gamma);
}

static int reset_gamma(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    CK(sd_bus_message_read(m, "s", &name));

    const float gamma[3] = { 1.0f, 1.0f, 1.0f };
    return apply_gamma(m, userdata, name, gamma);
}

static const sd_bus_vtable gamma_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListOutputs", "", "a(sbbddd)", list_outputs, 0),
    SD_BUS_METHOD("GetGamma", "s", "ddd", get_gamma, 0),
    SD_BUS_METHOD("SetGamma", "sddd", "", set_gamma, 0),
    SD_BUS_METHOD("ResetGamma", "s", "", reset_gamma, 0),
    SD_BUS_SIGNAL("GammaChanged", "sddd", 0),
    SD_BUS_VTABLE_END,
};

bool output_gamma_dbus_init(struct output_manager *output_manager)
{
    if (!dbus_register_object(gamma_service, gamma_path, gamma_interface, gamma_vtable,
                              output_manager)) {
        kywc_log(KYWC_WARN, "%s register failed", gamma_interface);
        return false;
    }
    return true;
}
