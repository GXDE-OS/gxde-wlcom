// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include "config.h"
#include "effect_p.h"
#include "util/dbus.h"

#define ENABLED_KEY "enabled"

static const char *service_path = "/com/kylin/Wlcom/Effect";
static const char *service_interface = "com.kylin.Wlcom.Effect";

static int list_effects(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct effect_manager *manager = userdata;

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(sub)"));

    struct effect *effect;
    wl_list_for_each(effect, &manager->effects, link) {
        sd_bus_message_append(reply, "(sub)", effect->name, effect->priority, effect->enabled);
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static int print_effect_options(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    CK(sd_bus_message_read(m, "s", &name));

    struct effect *effect = effect_by_name(name);
    if (!effect || !effect->options) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid effect name or no option.");
        return sd_bus_reply_method_error(m, &error);
    }

    const char *config = json_object_to_json_string(effect->options);
    return sd_bus_reply_method_return(m, "s", config);
}

static int enable_effect(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    int enabled = false;
    CK(sd_bus_message_read(m, "sb", &name, &enabled));

    struct effect *effect = effect_by_name(name);
    if (!effect) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid effect name.");
        return sd_bus_reply_method_error(m, &error);
    }

    effect_set_enabled(effect, enabled);
    return sd_bus_reply_method_return(m, NULL);
}

static int set_effect_option(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL, *option = NULL, *type = NULL;
    CK(sd_bus_message_read(m, "ss", &name, &option));

    struct effect *effect = effect_by_name(name);
    if (!effect) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid effect name.");
        return sd_bus_reply_method_error(m, &error);
    }

    struct effect_option opt = { option, { 0 } };
    enum json_type opt_type = json_type_null;

    CK(sd_bus_message_peek_type(m, NULL, &type));
    if (strcmp(type, "i") == 0) {
        CK(sd_bus_message_read(m, "v", "i", &opt.value.num));
        opt_type = json_type_int;
    } else if (strcmp(type, "b") == 0) {
        CK(sd_bus_message_read(m, "v", "b", &opt.value.boolean));
        opt_type = json_type_boolean;
    } else if (strcmp(type, "d") == 0) {
        CK(sd_bus_message_read(m, "v", "d", &opt.value.realnum));
        opt_type = json_type_double;
    } else if (strcmp(type, "s") == 0) {
        CK(sd_bus_message_read(m, "v", "s", &opt.value.str));
        opt_type = json_type_string;
    } else {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid option type.");
        return sd_bus_reply_method_error(m, &error);
    }

    json_object *data;
    if (!json_object_object_get_ex(effect->options, option, &data) ||
        json_object_get_type(data) != opt_type) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "option not found or type error.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (!effect->impl->configure) {
#if 0
        /* apply enabled key if no configure support */
        if (strcmp(option, ENABLED_KEY) == 0) {
            json_object_set_boolean(data, opt.value.boolean);
            return sd_bus_reply_method_return(m, NULL);
        }
#endif
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Effect is not support configure.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (effect->impl->configure(effect, &opt)) {
        switch (opt_type) {
        case json_type_boolean:
            json_object_set_boolean(data, opt.value.boolean);
            break;
        case json_type_double:
            json_object_set_double(data, opt.value.realnum);
            break;
        case json_type_int:
            json_object_set_int(data, opt.value.num);
            break;
        case json_type_string:
            json_object_set_string(data, opt.value.str);
            break;
        default:
            break;
        }
    }

    return sd_bus_reply_method_return(m, NULL);
}

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListAllEffects", "", "a(sub)", list_effects, 0),
    /* enable or disable effect in runtime */
    SD_BUS_METHOD("EnableEffect", "sb", "", enable_effect, 0),
    SD_BUS_METHOD("PrintEffectOptions", "s", "s", print_effect_options, 0),
    SD_BUS_METHOD("SetEffectOption", "ssv", "", set_effect_option, 0),
    // SD_BUS_METHOD("SetEffectOptions", "sa(sv)", "", set_effect_options, 0),
    SD_BUS_VTABLE_END,
};

bool effect_manager_config_init(struct effect_manager *effect_manager)
{
    effect_manager->config = config_manager_add_config("Effects");
    if (!effect_manager->config) {
        return false;
    }

    return dbus_register_object(NULL, service_path, service_interface, service_vtable,
                                effect_manager);
}

bool effect_init_config(struct effect *effect)
{
    struct effect_manager *manager = effect->manager;
    if (!manager->config) {
        return effect->enabled;
    }

    effect->options = json_object_object_get(manager->config->json, effect->name);
    if (!effect->options) {
        effect->options = json_object_new_object();
        json_object_object_add(manager->config->json, effect->name, effect->options);
    }

    bool enabled = effect->enabled;
    /* use state in configure file */
    json_object *data;
    if (json_object_object_get_ex(effect->options, ENABLED_KEY, &data)) {
        enabled = json_object_get_boolean(data);
    } else {
        json_object_object_add(effect->options, ENABLED_KEY, json_object_new_boolean(enabled));
    }

    return enabled;
}

bool effect_get_option_boolean(struct effect *effect, const char *key, bool value)
{
    if (!effect || !effect->options) {
        return value;
    }

    json_object *data;
    if (json_object_object_get_ex(effect->options, key, &data)) {
        return json_object_get_boolean(data);
    }

    json_object_object_add(effect->options, key, json_object_new_boolean(value));
    return value;
}

void effect_set_option_boolean(struct effect *effect, const char *key, bool value)
{
    if (!effect || !effect->options) {
        return;
    }
    json_object_object_add(effect->options, key, json_object_new_boolean(value));
}

int effect_get_option_int(struct effect *effect, const char *key, int value)
{
    if (!effect || !effect->options) {
        return value;
    }

    json_object *data;
    if (json_object_object_get_ex(effect->options, key, &data)) {
        return json_object_get_int(data);
    }

    json_object_object_add(effect->options, key, json_object_new_int(value));
    return value;
}

void effect_set_option_int(struct effect *effect, const char *key, int value)
{
    if (!effect || !effect->options) {
        return;
    }
    json_object_object_add(effect->options, key, json_object_new_int(value));
}

double effect_get_option_double(struct effect *effect, const char *key, double value)
{
    if (!effect || !effect->options) {
        return value;
    }

    json_object *data;
    if (json_object_object_get_ex(effect->options, key, &data)) {
        return json_object_get_double(data);
    }

    json_object_object_add(effect->options, key, json_object_new_double(value));
    return value;
}

void effect_set_option_double(struct effect *effect, const char *key, double value)
{
    if (!effect || !effect->options) {
        return;
    }
    json_object_object_add(effect->options, key, json_object_new_double(value));
}

const char *effect_get_option_string(struct effect *effect, const char *key, const char *value)
{
    if (!effect || !effect->options) {
        return value;
    }

    json_object *data;
    if (json_object_object_get_ex(effect->options, key, &data)) {
        return json_object_get_string(data);
    }

    json_object_object_add(effect->options, key, json_object_new_string(value));
    return value;
}

void effect_set_option_string(struct effect *effect, const char *key, const char *value)
{
    if (!effect || !effect->options) {
        return;
    }
    json_object_object_add(effect->options, key, json_object_new_string(value));
}

void effect_write_enabled_option(struct effect *effect, bool enabled)
{
    effect_set_option_boolean(effect, ENABLED_KEY, enabled);
}

bool effect_option_is_enabled_option(const struct effect_option *option)
{
    return option->key && strcmp(option->key, ENABLED_KEY) == 0;
}
