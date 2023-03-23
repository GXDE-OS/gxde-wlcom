#include "config.h"
#include "logger.h"

static const char *service_path = "/com/kylin/Wlcom";
static const char *service_interface = "com.kylin.Wlcom";

static int set_log_level(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    uint32_t level = 0;
    CK(sd_bus_message_read(m, "u", &level));

    if (level < KYWC_LOG_LEVEL_LAST) {
        logger_set_level(level);
        return sd_bus_reply_method_return(m, NULL);
    }

    const sd_bus_error error =
        SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild args, please input [0-5].");
    return sd_bus_reply_method_error(m, &error);
}

static int print_config(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct config_manager *cm = userdata;
    sd_bus_message *reply = NULL;

    CK(sd_bus_message_new_method_return(m, &reply));
    const char *config = json_object_to_json_string(cm->json);
    sd_bus_message_append_basic(reply, 's', config);
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 0;
}

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("SetLogLevel", "u", "", set_log_level, 0),
    SD_BUS_METHOD("PrintConfig", "", "s", print_config, 0),
    SD_BUS_VTABLE_END,
};

bool config_manager_common_init(struct config_manager *config_manager)
{
    return !!config_manager_add_config(NULL, service_path, service_interface, service_vtable,
                                       config_manager);
}
