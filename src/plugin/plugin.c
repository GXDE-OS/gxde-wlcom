// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>

#include <kywc/log.h>

#include "plugin.h"
#include "server.h"

static struct plugin_manager *plugin_manager = NULL;

static bool plugin_is_found(const char *name)
{
    struct plugin *plugin;
    wl_list_for_each(plugin, &plugin_manager->plugins, link) {
        if (!strcmp(plugin->name, name)) {
            return true;
        }
    }

    return false;
}

static void scan_all_plugins(bool rescan)
{
    kywc_log(KYWC_DEBUG, "Search all plugins in %s", PLUGIN_DIR);

    DIR *plugin_dir = opendir(PLUGIN_DIR);
    if (!plugin_dir) {
        kywc_log_errno(KYWC_INFO, "Plugin dir open failed");
        return;
    }

    struct dirent *ent;
    struct stat s;
    while ((ent = readdir(plugin_dir))) {
        char *path = malloc(strlen(ent->d_name) + strlen(PLUGIN_DIR) + 2);
        strcpy(path, PLUGIN_DIR "/");
        strcat(path, ent->d_name);
        lstat(path, &s);
        if (!S_ISREG(s.st_mode) || ent->d_name[0] == '.') {
            free(path);
            continue;
        }

        /* only search in one level, check file name */
        size_t index = strlen(ent->d_name) - 3;
        if (strcasecmp(ent->d_name + index, ".so")) {
            free(path);
            continue;
        }

        /* for plugin name */
        ent->d_name[index] = '\0';

        if (rescan && plugin_is_found(ent->d_name)) {
            kywc_log(KYWC_DEBUG, "Plugin %s is already found", ent->d_name);
            free(path);
            continue;
        }

        /* create a plugin and insert to plugin manager */
        struct plugin *plugin = calloc(1, sizeof(struct plugin));
        if (!plugin) {
            free(path);
            return;
        }

        plugin->name = strdup(ent->d_name);
        plugin->path = path;

        plugin->manager = plugin_manager;
        wl_list_init(&plugin->entries);
        wl_list_insert(&plugin_manager->plugins, &plugin->link);
    }

    closedir(plugin_dir);
}

void plugin_manager_rescan_plugin(void)
{
    scan_all_plugins(true);
}

static struct kywc_plugin_option *find_option_by_token(struct plugin *plugin, int32_t token,
                                                       option_type type)
{
    struct plugin_config_entry *entry;
    wl_list_for_each(entry, &plugin->entries, link) {
        struct kywc_plugin_option *option = &entry->option;
        if (option->token == token && option->type == type) {
            return option;
        }
    }

    return NULL;
}

bool kywc_plugin_get_option_boolean(void *plugin, int32_t token, bool *value)
{
    struct kywc_plugin_option *option = find_option_by_token(plugin, token, option_type_boolean);
    if (!option) {
        return false;
    }

    if (value) {
        *value = option->value.boolean;
    }
    return true;
}

bool kywc_plugin_get_option_double(void *plugin, int32_t token, double *value)
{
    struct kywc_plugin_option *option = find_option_by_token(plugin, token, option_type_double);
    if (!option) {
        return false;
    }

    if (value) {
        *value = option->value.realnum;
    }
    return true;
}

bool kywc_plugin_get_option_int(void *plugin, int32_t token, int *value)
{
    struct kywc_plugin_option *option = find_option_by_token(plugin, token, option_type_int);
    if (!option) {
        return false;
    }

    if (value) {
        *value = option->value.num;
    }
    return true;
}

const char *kywc_plugin_get_option_string(void *plugin, int32_t token)
{
    struct kywc_plugin_option *option = find_option_by_token(plugin, token, option_type_string);
    if (!option) {
        return NULL;
    }

    return option->value.str;
}

static void cleanup_plugin(struct plugin *plugin)
{
    wl_list_remove(&plugin->link);
    free((void *)plugin->name);
    free((void *)plugin->path);
    free(plugin);
}

static void plugin_destroy_config_entry(struct plugin_config_entry *entry)
{
    wl_list_remove(&entry->link);
    if (entry->option.type == option_type_string) {
        free((void *)entry->option.value.str);
    }
    free(entry);
}

static struct kywc_plugin_option *check_option(struct plugin *plugin,
                                               struct kywc_plugin_option *option)
{
    struct kywc_plugin_option *default_option = plugin->options;
    if (!default_option) {
        return NULL;
    }

    /* search this option in plugin->options */
    while (default_option->name) {
        if (!kywc_plugin_option_match(default_option, option)) {
            default_option++;
            continue;
        }
        return default_option;
    }

    return NULL;
}

static void plugin_merge_options(struct plugin *plugin)
{
    uint64_t mask = 0;

    /* read options from config */
    plugin_read_config(plugin);

    struct kywc_plugin_option *option;
    struct plugin_config_entry *entry, *entry_tmp;
    wl_list_for_each_safe(entry, entry_tmp, &plugin->entries, link) {
        option = check_option(plugin, &entry->option);
        /* remove if not support */
        if (!option) {
            plugin_destroy_config_entry(entry);
            continue;
        }

        entry->option.token = option->token;
        entry->default_value = option->value;
        mask |= 0x1ull << (option - plugin->options);
    }

    /* merge plugin default options */
    option = plugin->options;
    while (option && option->name) {
        /* this option was not configured */
        if (!((mask >> (option - plugin->options)) & 0x1ull)) {
            struct plugin_config_entry *entry = calloc(1, sizeof(struct plugin_config_entry));
            if (!entry) {
                continue;
            }

            entry->default_value = option->value;
            entry->option = *option;
            if (option->type == option_type_string) {
                entry->option.value.str = strdup(option->value.str);
            }
            wl_list_insert(&plugin->entries, &entry->link);
        }
        option++;
    }
}

static bool load_plugin(struct plugin *plugin, bool force)
{
    /* disabled in config */
    if (!plugin_need_load(plugin) && !force) {
        return false;
    }

    kywc_log(KYWC_DEBUG, "Load plugin %s", plugin->path);
    void *handle = dlopen(plugin->path, RTLD_LAZY | RTLD_GLOBAL);
    if (!handle) {
        kywc_log(KYWC_ERROR, "Failed to load %s: %s", plugin->name, dlerror());
        return false;
    }

    size_t len = strlen(plugin->name) + strlen("_plugin_data") + 1;
    char *symbol = calloc(len, sizeof(char));
    snprintf(symbol, len, "%s_plugin_data", plugin->name);

    struct kywc_plugin_data *pdata = dlsym(handle, symbol);
    free(symbol);
    /* check plugin data */
    if (!pdata || !pdata->info || !pdata->setup || !pdata->teardown) {
        kywc_log(KYWC_ERROR, "%s: invalid symbol", plugin->name);
        dlclose(handle);
        return false;
    }

    // TODO: check version

    plugin->info = pdata->info;
    plugin->options = pdata->options;
    plugin->setup = pdata->setup;
    plugin->option = pdata->option;
    plugin->teardown = pdata->teardown;

    plugin->handle = handle;
    plugin->loaded = true;

    /* merge plugin options with config and default */
    plugin_merge_options(plugin);

    return true;
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&plugin_manager->server_destroy.link);

    struct plugin *plugin, *plugin_tmp;
    wl_list_for_each_safe(plugin, plugin_tmp, &plugin_manager->plugins, link) {
        plugin_manager_unload_plugin(plugin);
        cleanup_plugin(plugin);
    }

    free(plugin_manager);
    plugin_manager = NULL;
}

struct plugin_manager *plugin_manager_create(struct server *server)
{
    plugin_manager = calloc(1, sizeof(struct plugin_manager));
    if (!plugin_manager) {
        return NULL;
    }

    wl_list_init(&plugin_manager->plugins);
    plugin_manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &plugin_manager->server_destroy);

    plugin_manager_config_init(plugin_manager);

    /* scan all plugins in plugin dir */
    scan_all_plugins(false);

    struct plugin *plugin, *plugin_tmp;
    wl_list_for_each_safe(plugin, plugin_tmp, &plugin_manager->plugins, link) {
        /* load plugin, remove if invaild */
        if (!load_plugin(plugin, false) || !plugin_manager_enable_plugin(plugin, true)) {
            plugin_manager_unload_plugin(plugin);
            // cleanup_plugin(plugin);
        }
    }

    return plugin_manager;
}

struct plugin *plugin_manager_get_plugin(const char *name)
{
    if (!name || !strlen(name)) {
        return NULL;
    }

    struct plugin *plugin;
    wl_list_for_each(plugin, &plugin_manager->plugins, link) {
        if (!strcmp(name, plugin->name)) {
            return plugin;
        }
    }

    return NULL;
}

struct plugin *plugin_manager_load_plugin(struct plugin *plugin)
{
    if (!plugin->loaded && !load_plugin(plugin, true)) {
        return NULL;
    }

    return plugin;
}

bool plugin_manager_enable_plugin(struct plugin *plugin, bool enable)
{
    if (enable == plugin->enabled) {
        return false;
    }
    if (!plugin->loaded) {
        kywc_log(KYWC_WARN, "plugin %s is not loaded when enable", plugin->name);
        return false;
    }

    if (enable) {
        if (!plugin->setup(plugin, &plugin->teardown_data)) {
            kywc_log(KYWC_ERROR, "plugin %s setup failed", plugin->name);
            return false;
        }
    } else {
        plugin->teardown(plugin->teardown_data);
    }

    plugin->enabled = enable;
    return true;
}

void plugin_manager_unload_plugin(struct plugin *plugin)
{
    if (plugin->loaded) {
        plugin_manager_enable_plugin(plugin, false);
        dlclose(plugin->handle);
    }

    /* free all config entries */
    struct plugin_config_entry *entry, *tmp_entry;
    wl_list_for_each_safe(entry, tmp_entry, &plugin->entries, link) {
        plugin_destroy_config_entry(entry);
    }

    plugin->loaded = false;
}
