// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include "plugin.h"
#include "util/dbus.h"

static const char *service_path = "/com/kylin/Wlcom/Plugin";
static const char *service_interface = "com.kylin.Wlcom.Plugin";

static void option_to_json(struct kywc_plugin_option *option, json_object *parent)
{
    json_object *value;

    switch (option->type) {
    case option_type_boolean:
        value = json_object_new_boolean(option->value.boolean);
        break;
    case option_type_double:
        value = json_object_new_double(option->value.realnum);
        break;
    case option_type_int:
        value = json_object_new_int(option->value.num);
        break;
    case option_type_string:
        value = json_object_new_string(option->value.str);
        break;
    default:
        return;
    }

    json_object_object_add(parent, option->name, value);
}

static int list_plugins(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct plugin_manager *pm = userdata;
    int rescan = false;
    CK(sd_bus_message_read(m, "b", &rescan));

    if (rescan) {
        plugin_manager_rescan_plugin();
    }

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(sbb)"));
    struct plugin *plugin;
    wl_list_for_each(plugin, &pm->plugins, link) {
        CK(sd_bus_message_append(reply, "(sbb)", plugin->name, plugin->loaded, plugin->enabled));
    }
    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static int print_plugin_info(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    CK(sd_bus_message_read(m, "s", &name));

    struct plugin *plugin = plugin_manager_get_plugin(name);
    if (!plugin) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid plugin name.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (!plugin->loaded) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Plugin is not loaded.");
        return sd_bus_reply_method_error(m, &error);
    }

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));
    sd_bus_message_append(reply, "sssuu", plugin->info->vendor, plugin->info->class,
                          plugin->info->description, plugin->info->version,
                          plugin->info->abi_version);
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static int print_plugin_config(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    CK(sd_bus_message_read(m, "s", &name));

    struct plugin *plugin = plugin_manager_get_plugin(name);
    if (!plugin) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid plugin name.");
        return sd_bus_reply_method_error(m, &error);
    }

    json_object *config = json_object_new_object();
    json_object_object_add(config, "enabled", json_object_new_boolean(plugin->enabled));
    json_object *options = json_object_new_object();
    json_object_object_add(config, "options", options);

    struct plugin_config_entry *entry;
    wl_list_for_each_reverse(entry, &plugin->entries, link) {
        option_to_json(&entry->option, options);
    }

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));
    const char *cfg = json_object_to_json_string(config);
    sd_bus_message_append_basic(reply, 's', cfg);
    json_object_put(config);

    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static int load_plugin(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    int loaded = false;
    CK(sd_bus_message_read(m, "sb", &name, &loaded));

    struct plugin *plugin = plugin_manager_get_plugin(name);
    if (!plugin) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid plugin name.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (loaded) {
        plugin_manager_load_plugin(plugin);
    } else {
        plugin_manager_unload_plugin(plugin);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int enable_plugin(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL;
    int enabled = false;
    CK(sd_bus_message_read(m, "sb", &name, &enabled));

    struct plugin *plugin = plugin_manager_get_plugin(name);
    if (!plugin) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid plugin name.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (!plugin->loaded) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Plugin is not loaded.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (plugin_manager_enable_plugin(plugin, enabled)) {
        plugin_write_config(plugin);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int set_plugin_option(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name = NULL, *option = NULL, *type = NULL;
    CK(sd_bus_message_read(m, "ss", &name, &option));

    struct plugin *plugin = plugin_manager_get_plugin(name);
    if (!plugin) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid plugin name.");
        return sd_bus_reply_method_error(m, &error);
    }

    CK(sd_bus_message_peek_type(m, NULL, &type));
    struct kywc_plugin_option opt = { option, { 0 }, option_type_null, -1 };
    if (!strcmp(type, "i")) {
        CK(sd_bus_message_read(m, "v", "i", &opt.value.num));
        opt.type = option_type_int;
    } else if (!strcmp(type, "b")) {
        CK(sd_bus_message_read(m, "v", "b", &opt.value.boolean));
        opt.type = option_type_boolean;
    } else if (!strcmp(type, "d")) {
        CK(sd_bus_message_read(m, "v", "d", &opt.value.realnum));
        opt.type = option_type_double;
    } else if (!strcmp(type, "s")) {
        CK(sd_bus_message_read(m, "v", "s", &opt.value.str));
        opt.type = option_type_string;
    } else {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid option type.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (!plugin->loaded || !plugin->enabled) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Plugin is not loaded or enabled.");
        return sd_bus_reply_method_error(m, &error);
    }

    /* find config entry in entries */
    struct plugin_config_entry *entry;
    wl_list_for_each(entry, &plugin->entries, link) {
        if (!kywc_plugin_option_match(&entry->option, &opt)) {
            continue;
        }

        /* option value is not changed */
        if (kywc_plugin_option_value_equal(entry->option.value, opt.value, entry->option.type)) {
            return sd_bus_reply_method_return(m, NULL);
        }

        if (opt.type == option_type_string) {
            free((void *)entry->option.value.str);
            entry->option.value.str = strdup(opt.value.str);
        } else {
            entry->option.value.str = opt.value.str;
        }
        plugin->option(&entry->option);
        plugin_write_config(plugin);
        return sd_bus_reply_method_return(m, NULL);
    }

    const sd_bus_error error =
        SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "option not found or type error.");
    return sd_bus_reply_method_error(m, &error);
}

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListAllPlugins", "b", "a(sbb)", list_plugins, 0),
    SD_BUS_METHOD("PrintPluginInfo", "s", "sssuu", print_plugin_info, 0),
    SD_BUS_METHOD("PrintPluginConfig", "s", "s", print_plugin_config, 0),
    SD_BUS_METHOD("LoadPlugin", "sb", "", load_plugin, 0),
    SD_BUS_METHOD("EnablePlugin", "sb", "", enable_plugin, 0),
    SD_BUS_METHOD("SetPluginOption", "ssv", "", set_plugin_option, 0),
    SD_BUS_VTABLE_END,
};

bool plugin_manager_config_init(struct plugin_manager *plugin_manager)
{
    plugin_manager->config = config_manager_add_config("plugins");
    if (!plugin_manager->config) {
        return false;
    }

    return dbus_register_object(NULL, service_path, service_interface, service_vtable,
                                plugin_manager);
}

static void option_from_json(struct kywc_plugin_option *option, json_object *value)
{
    enum json_type type = json_object_get_type(value);
    switch (type) {
    case json_type_boolean:
        option->type = option_type_boolean;
        option->value.boolean = json_object_get_boolean(value);
        break;
    case json_type_double:
        option->type = option_type_double;
        option->value.realnum = json_object_get_double(value);
        break;
    case json_type_int:
        option->type = option_type_int;
        option->value.num = json_object_get_int(value);
        break;
    case json_type_string:
        option->type = option_type_string;
        option->value.str = strdup(json_object_get_string(value));
        break;
    default:
        break;
    }
}

bool plugin_need_load(struct plugin *plugin)
{
    struct plugin_manager *pm = plugin->manager;
    json_object *config, *data;

    if (pm->config && pm->config->json &&
        (config = json_object_object_get(pm->config->json, plugin->name)) &&
        json_object_object_get_ex(config, "enabled", &data)) {
        return json_object_get_boolean(data);
    }

    return true;
}

void plugin_read_config(struct plugin *plugin)
{
    struct plugin_manager *pm = plugin->manager;
    if (!pm->config || !pm->config->json) {
        return;
    }

    /* get this plugin's config */
    json_object *config = json_object_object_get(pm->config->json, plugin->name);
    /* if no config, default to enable */
    if (!config) {
        return;
    }

    json_object *data;
    /* no options found in config */
    if (!json_object_object_get_ex(config, "options", &data)) {
        return;
    }

    json_object_object_foreach(data, key, value) {
        struct plugin_config_entry *entry = calloc(1, sizeof(struct plugin_config_entry));
        if (!entry) {
            continue;
        }

        entry->option.name = key;
        option_from_json(&entry->option, value);
        wl_list_insert(&plugin->entries, &entry->link);
    }
}

void plugin_write_config(struct plugin *plugin)
{
    struct plugin_manager *pm = plugin->manager;
    if (!pm->config || !pm->config->json) {
        return;
    }

    json_object *config = json_object_object_get(pm->config->json, plugin->name);
    if (!config) {
        config = json_object_new_object();
        json_object_object_add(pm->config->json, plugin->name, config);
    }

    json_object_object_add(config, "enabled", json_object_new_boolean(plugin->enabled));
    json_object *options = json_object_object_get(config, "options");
    if (!options) {
        options = json_object_new_object();
        json_object_object_add(config, "options", options);
    }

    struct plugin_config_entry *entry;
    wl_list_for_each(entry, &plugin->entries, link) {
        /* filter default option */
        if (kywc_plugin_option_value_equal(entry->option.value, entry->default_value,
                                           entry->option.type)) {
            json_object_object_del(options, entry->option.name);
            continue;
        }
        option_to_json(&entry->option, options);
    }
}
