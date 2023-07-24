#include <stdlib.h>

#include "config.h"
#include "theme_p.h"

static const char *service_path = "/com/kylin/Wlcom/Theme";
static const char *service_interface = "com.kylin.Wlcom.Theme";

static int list_themes(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    struct theme_manager *manager = userdata;

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(msg, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "s"));

    struct theme *theme;
    wl_list_for_each(theme, &manager->themes, link) {
        sd_bus_message_append(reply, "s", theme->theme_name);
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static int current_theme(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    struct theme_manager *manager = userdata;
    return sd_bus_reply_method_return(msg, "s", manager->current->theme_name);
}

static int set_theme(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    char *theme = NULL;
    CK(sd_bus_message_read(msg, "s", &theme));
    int32_t ret = theme_manager_set_theme(theme);
    return sd_bus_reply_method_return(msg, "b", &ret);
}

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListAllThemes", "", "as", list_themes, 0),
    SD_BUS_METHOD("currentTheme", "", "s", current_theme, 0),
    SD_BUS_METHOD("SetTheme", "s", "b", set_theme, 0),
    SD_BUS_VTABLE_END,
};

bool theme_manager_config_init(struct theme_manager *manager)
{
    manager->config = config_manager_add_config("theme", NULL, service_path, service_interface,
                                                service_vtable, manager);
    return !!manager->config;
}

const char *theme_manager_read_config(struct theme_manager *manager)
{
    if (!manager->config || !manager->config->json) {
        return NULL;
    }

    json_object *data;
    if (json_object_object_get_ex(manager->config->json, "name", &data)) {
        return json_object_get_string(data);
    }

    return NULL;
}

void theme_manager_write_config(struct theme_manager *manager, const char *name)
{
    if (name && name[0]) {
        json_object_object_add(manager->config->json, "name", json_object_new_string(name));
    }
}
