#include <stdio.h>
#include <unistd.h>

#include <kywc/log.h>
#include <sys/stat.h>

#include "config_p.h"
#include "server.h"

static struct config_manager *config_manager = NULL;

static char *check_config_file(void)
{
    /* get config path */
    const char *home = getenv("HOME");
    char *config_home_fallback = NULL;

    const char *config_home = getenv("XDG_CONFIG_HOME");
    if ((!config_home || config_home[0] == '\0') && home) {
        size_t size_fallback = 1 + strlen(home) + strlen("/.config");
        config_home_fallback = calloc(size_fallback, sizeof(char));
        if (config_home_fallback != NULL) {
            snprintf(config_home_fallback, size_fallback, "%s/.config", home);
        }
        config_home = config_home_fallback;
    }

    const char *config_folder = "kylin-wlcom";
    const char *filename = "config.json";
    size_t folder = 1 + strlen(config_home) + strlen(config_folder);

    size_t size = 3 + strlen(config_home) + strlen(config_folder) + strlen(filename);
    char *path = calloc(size, sizeof(char));
    snprintf(path, size, "%s/%s/%s", config_home, config_folder, filename);
    free(config_home_fallback);
    if (!path) {
        return NULL;
    }

    /* now check config folder */
    path[folder] = '\0';
    int ret = access(path, F_OK);
    if (ret) {
        kywc_log(KYWC_INFO, "configure dir %s not exist, create it", path);
        ret = mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        if (ret) {
            kywc_log_errno(KYWC_ERROR, "create configure dir failed");
            goto err;
        }
    }

    path[folder] = '/';
    return path;

err:
    free(path);
    return NULL;
}

static int dbus_process(int fd, uint32_t mask, void *data)
{
    if (mask & WL_EVENT_ERROR) {
        kywc_log(KYWC_ERROR, "IPC Client error");
        return 0;
    }
    if (mask & WL_EVENT_HANGUP) {
        kywc_log(KYWC_DEBUG, "Client hung up");
        return 0;
    }

    while (sd_bus_process(config_manager->bus, NULL) > 0) {
        ;
    }
    return 0;
}

static bool dbus_init(struct wl_event_loop *loop)
{
    int ret = sd_bus_open_user(&config_manager->bus);
    if (ret < 0) {
        kywc_log(KYWC_ERROR, "Failed to connect to user bus: %s", strerror(-ret));
        return false;
    }

    const char *service_name = "com.kylin.Wlcom";
    ret = sd_bus_request_name(config_manager->bus, service_name, 0);
    if (ret < 0) {
        kywc_log(KYWC_ERROR, "Failed to acquire service name: %s", strerror(-ret));
        if (ret == -EEXIST) {
            kywc_log(KYWC_ERROR, "Is a Kylin-Wlcom already running?");
        }
        sd_bus_flush_close_unref(config_manager->bus);
        return false;
    }

    int fd = sd_bus_get_fd(config_manager->bus);
    config_manager->event = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, dbus_process, NULL);
    return true;
}

// TODO: add config arg to check synced flag
void config_manager_sync(void)
{
    if (!config_manager->file || !config_manager->json) {
        return;
    }

    json_object_to_file_ext(config_manager->file, config_manager->json,
                            JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&config_manager->display_destroy.link);

    if (config_manager->event) {
        wl_event_source_remove(config_manager->event);
    }
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&config_manager->server_destroy.link);

    struct config *config, *config_tmp;
    wl_list_for_each_safe(config, config_tmp, &config_manager->configs, link) {
        wl_signal_emit_mutable(&config->events.destroy, NULL);
        wl_list_remove(&config->link);
        sd_bus_slot_unref(config->slot);
        free(config);
    }

    sd_bus_flush_close_unref(config_manager->bus);

    config_manager_sync();
    json_object_put(config_manager->json);
    free(config_manager->file);

    free(config_manager);
    config_manager = NULL;
}

static void handle_server_ready(struct wl_listener *listener, void *data)
{
    kde_global_accel_manager_create(config_manager);
    kde_input_manager_create(config_manager);
    kde_global_settings_create(config_manager);
}

struct config_manager *config_manager_create(struct server *server)
{
    config_manager = calloc(1, sizeof(struct config_manager));
    if (!config_manager) {
        return NULL;
    }

    /* read config file */
    config_manager->file = check_config_file();
    if (config_manager->file) {
        config_manager->json = json_object_from_file(config_manager->file);
    }

    /* create one if empty */
    if (!config_manager->json) {
        config_manager->json = json_object_new_object();
    }

    /* do nothing if dbus failed */
    if (!dbus_init(server->event_loop)) {
        config_manager->bus = NULL;
        kywc_log(KYWC_ERROR, "configure dbus server init failed");
    }

    config_manager->server = server;
    wl_list_init(&config_manager->configs);

    config_manager->server_ready.notify = handle_server_ready;
    wl_signal_add(&server->events.ready, &config_manager->server_ready);
    config_manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &config_manager->server_destroy);
    config_manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &config_manager->display_destroy);

    config_manager_common_init(config_manager);

    return config_manager;
}

struct config *config_manager_add_config(const char *name, const char *bus, const char *path,
                                         const char *interface, const sd_bus_vtable *vtable,
                                         void *data)
{
    struct config *config = calloc(1, sizeof(struct config));
    if (!config) {
        return NULL;
    }

    json_object *object = config_manager->json;
    if (name) {
        object = json_object_object_get(config_manager->json, name);
        if (!object) {
            object = json_object_new_object();
            json_object_object_add(config_manager->json, name, object);
        }
    }
    config->json = object;

    if (bus) {
        sd_bus_request_name(config_manager->bus, bus, 0);
    }

    if (path && interface && vtable) {
        sd_bus_add_object_vtable(config_manager->bus, &config->slot, path, interface, vtable, data);
    }

    wl_signal_init(&config->events.destroy);
    wl_list_insert(&config_manager->configs, &config->link);
    return config;
}
