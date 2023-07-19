#define _POSIX_C_SOURCE 200809L
#include <libinput.h>
#include <string.h>

#include <kywc/log.h>

#include "config_p.h"
#include "input/input.h"

static const char *service_path = "/org/kde/KWin/InputDevice";
static const char *kde_input_path = "/org/kde/KWin/InputDevice/";
static const char *service_interface = "org.kde.KWin.InputDeviceManager";
static const char *kde_input_interface = "org.kde.KWin.InputDevice";

#define KDE_PROP(name, type, read)                                                                 \
    SD_BUS_PROPERTY(name, type, read, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE)

#define KDE_WPROP(name, type, read, write)                                                         \
    SD_BUS_WRITABLE_PROPERTY(name, type, read, write, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE)

struct kde_input {
    struct wl_list link;
    struct config *config;
    char *sys_name;
    struct input *input;
    struct wl_listener destroy;
};

struct kde_input_manager {
    struct wl_list inputs;
    struct config *config;

    struct wl_listener new_input;
    struct wl_listener destroy;
};

static struct kde_input_manager *kde_input_manager = NULL;

static int current_state(sd_bus *bus, const char *path, const char *interface, const char *property,
                         sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    CK(sd_bus_message_open_container(reply, 'a', "s"));

    struct kde_input_manager *manager = userdata;
    struct kde_input *input;
    wl_list_for_each(input, &manager->inputs, link) {
        CK(sd_bus_message_append_basic(reply, 's', input->sys_name));
    }

    CK(sd_bus_message_close_container(reply));
    return 1;
}

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    KDE_PROP("devicesSysNames", "as", current_state),
    SD_BUS_VTABLE_END,
};

static int is_pointer(sd_bus *bus, const char *path, const char *interface, const char *property,
                      sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    /**
     * The client will differentiate between the pointer and the touchpad again,
     * and here any pointer will be returned without distinguishing between the pointer and the
     * touchpad.
     */
    uint32_t is_pointer = input->input->prop.type == WLR_INPUT_DEVICE_POINTER;
    return sd_bus_message_append_basic(reply, 'b', &is_pointer);
}

static int is_keyboard(sd_bus *bus, const char *path, const char *interface, const char *property,
                       sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t is_keyboard = input->input->prop.type == WLR_INPUT_DEVICE_KEYBOARD;
    return sd_bus_message_append_basic(reply, 'b', &is_keyboard);
}

static int is_tablet_tool(sd_bus *bus, const char *path, const char *interface,
                          const char *property, sd_bus_message *reply, void *userdata,
                          sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t is_tablet_tool = input->input->prop.type == WLR_INPUT_DEVICE_TABLET_TOOL;
    return sd_bus_message_append_basic(reply, 'b', &is_tablet_tool);
}

static int is_tablet_pad(sd_bus *bus, const char *path, const char *interface, const char *property,
                         sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t is_tablet_pad = input->input->prop.type == WLR_INPUT_DEVICE_TABLET_PAD;
    return sd_bus_message_append_basic(reply, 'b', &is_tablet_pad);
}

static int is_switch(sd_bus *bus, const char *path, const char *interface, const char *property,
                     sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t is_switch = input->input->prop.type == WLR_INPUT_DEVICE_SWITCH;
    return sd_bus_message_append_basic(reply, 'b', &is_switch);
}

static int name(sd_bus *bus, const char *path, const char *interface, const char *property,
                sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    return sd_bus_message_append_basic(reply, 's', input->input->wlr_input->name);
}

static int sys_name(sd_bus *bus, const char *path, const char *interface, const char *property,
                    sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    return sd_bus_message_append_basic(reply, 's', input->sys_name);
}

static int supports_pointer_acceleration(sd_bus *bus, const char *path, const char *interface,
                                         const char *property, sd_bus_message *reply,
                                         void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t acceleration = input->input->prop.has_pointer_accel;
    return sd_bus_message_append_basic(reply, 'b', &acceleration);
}

static int default_pointer_acceleration(sd_bus *bus, const char *path, const char *interface,
                                        const char *property, sd_bus_message *reply, void *userdata,
                                        sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    double acceleration = input->input->default_state.pointer_accel_speed;
    return sd_bus_message_append_basic(reply, 'd', &acceleration);
}

static int get_accel_speed(sd_bus *bus, const char *path, const char *interface,
                           const char *property, sd_bus_message *reply, void *userdata,
                           sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    double accel_speed = input->input->state.pointer_accel_speed;
    return sd_bus_message_append_basic(reply, 'd', &accel_speed);
}

static int set_accel_speed(sd_bus *bus, const char *path, const char *interface,
                           const char *property, sd_bus_message *reply, void *userdata,
                           sd_bus_error *ret_error)
{
    double accel_speed;
    CK(sd_bus_message_read(reply, "d", &accel_speed));

    struct kde_input *input = userdata;
    struct input_state state = input->input->state;
    state.pointer_accel_speed = accel_speed;
    input_set_state(input->input, &state);
    return sd_bus_reply_method_return(reply, NULL);
}

static int supports_pointer_acceleration_profile_adaptive(sd_bus *bus, const char *path,
                                                          const char *interface,
                                                          const char *property,
                                                          sd_bus_message *reply, void *userdata,
                                                          sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t acceleration =
        input->input->prop.accel_profiles & LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
    return sd_bus_message_append_basic(reply, 'b', &acceleration);
}

static int default_pointer_acceleration_profile_adaptive(sd_bus *bus, const char *path,
                                                         const char *interface,
                                                         const char *property,
                                                         sd_bus_message *reply, void *userdata,
                                                         sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t acceleration =
        input->input->default_state.accel_profile & LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
    return sd_bus_message_append_basic(reply, 'b', &acceleration);
}

static int get_accel_profile(sd_bus *bus, const char *path, const char *interface,
                             const char *property, sd_bus_message *reply, void *userdata,
                             sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t accel_profile =
        input->input->state.accel_profile & LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
    return sd_bus_message_append_basic(reply, 'b', &accel_profile);
}

static int set_accel_profile(sd_bus *bus, const char *path, const char *interface,
                             const char *property, sd_bus_message *reply, void *userdata,
                             sd_bus_error *ret_error)
{
    uint32_t accel_profile;
    CK(sd_bus_message_read(reply, "b", &accel_profile));

    struct kde_input *input = userdata;
    struct input_state state = input->input->state;
    state.accel_profile =
        accel_profile ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
    input_set_state(input->input, &state);
    return sd_bus_reply_method_return(reply, NULL);
}

static int supports_left_handed(sd_bus *bus, const char *path, const char *interface,
                                const char *property, sd_bus_message *reply, void *userdata,
                                sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t left_handed = input->input->prop.has_left_handed;
    return sd_bus_message_append_basic(reply, 'b', &left_handed);
}

static int left_handed_enabled_by_default(sd_bus *bus, const char *path, const char *interface,
                                          const char *property, sd_bus_message *reply,
                                          void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t left_handed = input->input->default_state.left_handed;
    return sd_bus_message_append_basic(reply, 'b', &left_handed);
}

static int get_left_handed(sd_bus *bus, const char *path, const char *interface,
                           const char *property, sd_bus_message *reply, void *userdata,
                           sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t left_handed = input->input->state.left_handed;
    return sd_bus_message_append_basic(reply, 'b', &left_handed);
}

static int set_left_handed(sd_bus *bus, const char *path, const char *interface,
                           const char *property, sd_bus_message *reply, void *userdata,
                           sd_bus_error *ret_error)
{
    uint32_t left_handed;
    CK(sd_bus_message_read(reply, "b", &left_handed));

    struct kde_input *input = userdata;
    struct input_state state = input->input->state;
    state.left_handed = left_handed;
    input_set_state(input->input, &state);
    return sd_bus_reply_method_return(reply, NULL);
}

static int supports_natural_scroll(sd_bus *bus, const char *path, const char *interface,
                                   const char *property, sd_bus_message *reply, void *userdata,
                                   sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t natural_scroll = input->input->prop.has_natural_scroll;
    return sd_bus_message_append_basic(reply, 'b', &natural_scroll);
}

static int natural_scroll_enabled_by_default(sd_bus *bus, const char *path, const char *interface,
                                             const char *property, sd_bus_message *reply,
                                             void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t natural_scroll = input->input->default_state.natural_scroll;
    return sd_bus_message_append_basic(reply, 'b', &natural_scroll);
}

static int get_natural_scroll(sd_bus *bus, const char *path, const char *interface,
                              const char *property, sd_bus_message *reply, void *userdata,
                              sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t natural_scroll = input->input->state.natural_scroll;
    return sd_bus_message_append_basic(reply, 'b', &natural_scroll);
}

static int set_natural_scroll(sd_bus *bus, const char *path, const char *interface,
                              const char *property, sd_bus_message *reply, void *userdata,
                              sd_bus_error *ret_error)
{
    uint32_t natural_scroll;
    CK(sd_bus_message_read(reply, "b", &natural_scroll));

    struct kde_input *input = userdata;
    struct input_state state = input->input->state;
    state.natural_scroll = natural_scroll;
    input_set_state(input->input, &state);
    return sd_bus_reply_method_return(reply, NULL);
}

static const sd_bus_vtable input_vtable[] = {
    SD_BUS_VTABLE_START(0),
    KDE_PROP("pointer", "b", is_pointer),
    KDE_PROP("keyboard", "b", is_keyboard),
    KDE_PROP("tabletTool", "b", is_tablet_tool),
    KDE_PROP("tabletPad", "b", is_tablet_pad),
    KDE_PROP("switchDevice", "b", is_switch),
    KDE_PROP("name", "s", name),
    KDE_PROP("sysName", "s", sys_name),

    KDE_PROP("supportsPointerAcceleration", "b", supports_pointer_acceleration),
    KDE_PROP("defaultPointerAcceleration", "d", default_pointer_acceleration),
    KDE_WPROP("pointerAcceleration", "d", get_accel_speed, set_accel_speed),

    KDE_PROP("supportsLeftHanded", "b", supports_left_handed),
    KDE_PROP("leftHandedEnabledByDefault", "b", left_handed_enabled_by_default),
    KDE_WPROP("leftHanded", "b", get_left_handed, set_left_handed),

    KDE_PROP("supportsPointerAccelerationProfileAdaptive", "b",
             supports_pointer_acceleration_profile_adaptive),
    KDE_PROP("defaultPointerAccelerationProfileAdaptive", "b",
             default_pointer_acceleration_profile_adaptive),
    KDE_WPROP("pointerAccelerationProfileAdaptive", "b", get_accel_profile, set_accel_profile),

    KDE_PROP("supportsNaturalScroll", "b", supports_natural_scroll),
    KDE_PROP("naturalScrollEnabledByDefault", "b", natural_scroll_enabled_by_default),
    KDE_WPROP("naturalScroll", "b", get_natural_scroll, set_natural_scroll),

    SD_BUS_VTABLE_END,
};

static void kde_input_destroy(struct kde_input *input)
{
    wl_list_remove(&input->link);
    wl_list_remove(&input->destroy.link);
    wl_list_remove(&input->config->link);

    wl_signal_emit_mutable(&input->config->events.destroy, NULL);
    sd_bus_slot_unref(input->config->slot);

    free(input->config);
    free(input->sys_name);
    free(input);
}

static void handle_kde_input_destroy(struct wl_listener *listener, void *data)
{
    struct kde_input *input = wl_container_of(listener, input, destroy);
    kde_input_destroy(input);
}

static void handle_new_kde_input(struct wl_listener *listener, void *data)
{
    struct input *input = data;
    if (!input->device) {
        return;
    }

    struct kde_input *kde_input = calloc(1, sizeof(struct kde_input));
    if (!kde_input) {
        return;
    }
    wl_list_insert(&kde_input_manager->inputs, &kde_input->link);

    kde_input->destroy.notify = handle_kde_input_destroy;
    wl_signal_add(&input->events.destroy, &kde_input->destroy);

    const char *sys_name = libinput_device_get_sysname(input->device);
    kde_input->sys_name = strdup(sys_name);
    kde_input->input = input;

    size_t size = 1 + strlen(kde_input_path) + strlen(sys_name);
    char *path = calloc(size, sizeof(char));
    snprintf(path, size, "%s%s", kde_input_path, kde_input->sys_name);
    kde_input->config =
        config_manager_add_config(NULL, NULL, path, kde_input_interface, input_vtable, kde_input);
    free(path);
}

static void handle_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&kde_input_manager->new_input.link);
    wl_list_remove(&kde_input_manager->destroy.link);

    /* destroy kde_input devices*/
    struct kde_input *input, *input_tmp;
    wl_list_for_each_safe(input, input_tmp, &kde_input_manager->inputs, link) {
        kde_input_destroy(input);
    }
    free(kde_input_manager);
    kde_input_manager = NULL;
}

bool kde_input_manager_create(struct server *server)
{
    kde_input_manager = calloc(1, sizeof(struct kde_input_manager));
    if (!kde_input_manager) {
        return false;
    }

    kde_input_manager->config = config_manager_add_config(
        NULL, "org.kde.KWin", service_path, service_interface, service_vtable, kde_input_manager);
    if (!kde_input_manager->config) {
        free(kde_input_manager);
        kde_input_manager = NULL;
        return false;
    }

    wl_list_init(&kde_input_manager->inputs);

    kde_input_manager->new_input.notify = handle_new_kde_input;
    input_add_new_listener(&kde_input_manager->new_input);

    kde_input_manager->destroy.notify = handle_destroy;
    wl_signal_add(&kde_input_manager->config->events.destroy, &kde_input_manager->destroy);

    return true;
}