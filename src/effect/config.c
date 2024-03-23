// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include "config.h"
#include "effect_p.h"

static const char *service_path = "/com/kylin/Wlcom/Effect";
static const char *service_interface = "com.kylin.Wlcom.Effect";

static int list_effects(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct effect_manager *manager = userdata;

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(ssub)"));

    struct effect *effect;
    wl_list_for_each(effect, &manager->effects, link) {
        sd_bus_message_append(reply, "(ssub)", effect->uuid, effect->name, effect->priority,
                              effect->enabled);
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static int enable_effect(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    int enabled = false;
    CK(sd_bus_message_read(m, "sb", &name, &enabled));

    struct effect *effect = effect_by_name(name);
    if (effect) {
        effect_set_enabled(effect, enabled);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListAllEffects", "", "a(ssub)", list_effects, 0),
    SD_BUS_METHOD("EnableEffect", "sb", "", enable_effect, 0),
    SD_BUS_VTABLE_END,
};

bool effect_manager_config_init(struct effect_manager *effect_manager)
{
    effect_manager->config = config_manager_add_config(
        "Effects", NULL, service_path, service_interface, service_vtable, effect_manager);

    return !!effect_manager->config;
}
