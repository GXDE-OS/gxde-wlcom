#ifndef _PLUGIN_H_
#define _PLUGIN_H_

#include <wayland-server-core.h>

#include <kywc/plugin.h>

struct server;

struct plugin_config_entry {
    struct wl_list link;
    struct kywc_plugin_option option;
    option_value default_value;
};

struct plugin {
    struct wl_list link;
    struct plugin_manager *manager;
    /* plugin name, maybe different from info->name */
    const char *name;
    /* full path of the plugin file */
    const char *path;

    struct kywc_plugin_info *info;
    struct kywc_plugin_option *options;
    plugin_setup_func setup;
    plugin_option_func option;
    plugin_teardown_func teardown;

    void *handle;
    void *teardown_data; /* return by setup_func */

    /* config entries */
    struct wl_list entries;
    bool loaded, enabled;
    // bool invaild;
};

struct plugin_manager {
    struct wl_list plugins;

    /* all plugins configurations */
    struct config *config;
    struct wl_listener server_destroy;
};

/**
 * load all plugins specificed in config file.
 */
struct plugin_manager *plugin_manager_create(struct server *server);

void plugin_manager_rescan_plugin(void);

/**
 * plugin manager configuration support
 */
bool plugin_manager_config_init(struct plugin_manager *plugin_manager);

bool plugin_need_load(struct plugin *plugin);

void plugin_read_config(struct plugin *plugin);

void plugin_write_config(struct plugin *plugin);

/**
 * load a plugin specificed by name.
 */
struct plugin *plugin_manager_get_plugin(const char *name);

struct plugin *plugin_manager_load_plugin(struct plugin *plugin);

void plugin_manager_unload_plugin(struct plugin *plugin);

bool plugin_manager_enable_plugin(struct plugin *plugin, bool enable);

#endif /* _PLUGIN_H_ */
