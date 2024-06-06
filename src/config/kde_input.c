// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <libinput.h>
#include <string.h>

#include "config_p.h"
#include "input/input.h"
#include "kywc/output.h"
#include "server.h"

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

static int is_touchpad(sd_bus *bus, const char *path, const char *interface, const char *property,
                       sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t is_touchpad =
        input->input->prop.type == WLR_INPUT_DEVICE_POINTER && input->input->prop.tap_finger_count;
    return sd_bus_message_append_basic(reply, 'b', &is_touchpad);
}

static int is_touch(sd_bus *bus, const char *path, const char *interface, const char *property,
                    sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t is_touch = input->input->prop.type == WLR_INPUT_DEVICE_TOUCH;
    return sd_bus_message_append_basic(reply, 'b', &is_touch);
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

static int product(sd_bus *bus, const char *path, const char *interface, const char *property,
                   sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t product = input->input->prop.product;
    return sd_bus_message_append_basic(reply, 'i', &product);
}

static int vendor(sd_bus *bus, const char *path, const char *interface, const char *property,
                  sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t vendor = input->input->prop.vendor;
    return sd_bus_message_append_basic(reply, 'i', &vendor);
}

static int supports_disable_events(sd_bus *bus, const char *path, const char *interface,
                                   const char *property, sd_bus_message *reply, void *userdata,
                                   sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t event = input->input->prop.send_events_modes;
    uint32_t enable = event & LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
    return sd_bus_message_append_basic(reply, 'b', &enable);
}

static int supports_disable_events_on_external_mouse(sd_bus *bus, const char *path,
                                                     const char *interface, const char *property,
                                                     sd_bus_message *reply, void *userdata,
                                                     sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t event = input->input->prop.send_events_modes;
    uint32_t enable = event & LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;
    return sd_bus_message_append_basic(reply, 'b', &enable);
}

static int get_touchpad_enable(sd_bus *bus, const char *path, const char *interface,
                               const char *property, sd_bus_message *reply, void *userdata,
                               sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t mode = input->input->state.send_events_mode;
    uint32_t enable = mode == LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
    return sd_bus_message_append_basic(reply, 'b', &enable);
}

static int set_touchpad_enable(sd_bus *bus, const char *_path, const char *interface,
                               const char *property, sd_bus_message *reply, void *userdata,
                               sd_bus_error *ret_error)
{
    uint32_t touchpad_enable;
    CK(sd_bus_message_read(reply, "b", &touchpad_enable));

    struct kde_input *input = userdata;
    struct input_state state = input->input->state;
    state.send_events_mode = touchpad_enable
                                 ? LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
                                 : LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;
    input_set_state(input->input, &state);
    return sd_bus_reply_method_return(reply, NULL);
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

static int tap_finger_count(sd_bus *bus, const char *path, const char *interface,
                            const char *property, sd_bus_message *reply, void *userdata,
                            sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t tap_finger_count = input->input->prop.tap_finger_count;
    return sd_bus_message_append_basic(reply, 'i', &tap_finger_count);
}

static int tap_to_click_enabled_by_default(sd_bus *bus, const char *path, const char *interface,
                                           const char *property, sd_bus_message *reply,
                                           void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t tap_to_click = input->input->default_state.tap_to_click;
    return sd_bus_message_append_basic(reply, 'b', &tap_to_click);
}

static int get_tap_click(sd_bus *bus, const char *path, const char *interface, const char *property,
                         sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t tap_to_click = input->input->state.tap_to_click;
    return sd_bus_message_append_basic(reply, 'b', &tap_to_click);
}

static int set_tap_click(sd_bus *bus, const char *path, const char *interface, const char *property,
                         sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    uint32_t is_tap_click;
    CK(sd_bus_message_read(reply, "b", &is_tap_click));

    struct kde_input *input = userdata;
    struct input_state state = input->input->state;
    state.tap_to_click = is_tap_click;
    input_set_state(input->input, &state);
    return sd_bus_reply_method_return(reply, NULL);
}

static int supports_scroll_two_finger(sd_bus *bus, const char *path, const char *interface,
                                      const char *property, sd_bus_message *reply, void *userdata,
                                      sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t scroll_methods = input->input->prop.scroll_methods;
    uint32_t two_finger = scroll_methods & LIBINPUT_CONFIG_SCROLL_2FG;
    return sd_bus_message_append_basic(reply, 'b', &two_finger);
}

static int scroll_two_finger_enabled_by_default(sd_bus *bus, const char *path,
                                                const char *interface, const char *property,
                                                sd_bus_message *reply, void *userdata,
                                                sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t scroll_methods = input->input->default_state.scroll_method;
    uint32_t two_finger = scroll_methods & LIBINPUT_CONFIG_SCROLL_2FG;
    return sd_bus_message_append_basic(reply, 'b', &two_finger);
}

static int get_scroll_two_finger(sd_bus *bus, const char *path, const char *interface,
                                 const char *property, sd_bus_message *reply, void *userdata,
                                 sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t scroll_methods = input->input->state.scroll_method;
    uint32_t two_finger = scroll_methods & LIBINPUT_CONFIG_SCROLL_2FG;
    return sd_bus_message_append_basic(reply, 'b', &two_finger);
}

static int set_scroll_two_finger(sd_bus *bus, const char *path, const char *interface,
                                 const char *property, sd_bus_message *reply, void *userdata,
                                 sd_bus_error *ret_error)
{
    uint32_t method;
    CK(sd_bus_message_read(reply, "b", &method));

    struct kde_input *input = userdata;
    int32_t current = input->input->state.scroll_method;
    method = method ? LIBINPUT_CONFIG_SCROLL_2FG : LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
    if ((current != LIBINPUT_CONFIG_SCROLL_2FG && method == LIBINPUT_CONFIG_SCROLL_2FG) ||
        (current != LIBINPUT_CONFIG_SCROLL_EDGE && method == LIBINPUT_CONFIG_SCROLL_NO_SCROLL)) {
        struct input_state state = input->input->state;
        state.scroll_method = method;
        input_set_state(input->input, &state);
    }
    return sd_bus_reply_method_return(reply, NULL);
}

static int supports_scroll_edge(sd_bus *bus, const char *path, const char *interface,
                                const char *property, sd_bus_message *reply, void *userdata,
                                sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t scroll_methods = input->input->prop.scroll_methods;
    uint32_t is_scroll_edge = scroll_methods & LIBINPUT_CONFIG_SCROLL_EDGE;
    return sd_bus_message_append_basic(reply, 'b', &is_scroll_edge);
}

static int scroll_edge_enabled_by_default(sd_bus *bus, const char *path, const char *interface,
                                          const char *property, sd_bus_message *reply,
                                          void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t scroll_method = input->input->default_state.scroll_method;
    uint32_t is_scroll_edge = scroll_method & LIBINPUT_CONFIG_SCROLL_EDGE;
    return sd_bus_message_append_basic(reply, 'b', &is_scroll_edge);
}

static int get_scroll_edge(sd_bus *bus, const char *path, const char *interface,
                           const char *property, sd_bus_message *reply, void *userdata,
                           sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t scroll_method = input->input->state.scroll_method;
    uint32_t is_scroll_edge = scroll_method & LIBINPUT_CONFIG_SCROLL_EDGE;
    return sd_bus_message_append_basic(reply, 'b', &is_scroll_edge);
}

static int set_scroll_edge(sd_bus *bus, const char *path, const char *interface,
                           const char *property, sd_bus_message *reply, void *userdata,
                           sd_bus_error *ret_error)
{
    uint32_t method;
    CK(sd_bus_message_read(reply, "b", &method));

    struct kde_input *input = userdata;
    uint32_t current = input->input->state.scroll_method;
    method = method ? LIBINPUT_CONFIG_SCROLL_EDGE : LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
    if ((current != LIBINPUT_CONFIG_SCROLL_EDGE && method == LIBINPUT_CONFIG_SCROLL_EDGE) ||
        (current != LIBINPUT_CONFIG_SCROLL_2FG && method == LIBINPUT_CONFIG_SCROLL_NO_SCROLL)) {
        struct input_state state = input->input->state;
        state.scroll_method = method;
        input_set_state(input->input, &state);
    }
    return sd_bus_reply_method_return(reply, NULL);
}

static int supports_disable_while_typing(sd_bus *bus, const char *path, const char *interface,
                                         const char *property, sd_bus_message *reply,
                                         void *userdata, sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t dwt = input->input->prop.has_dwt;
    return sd_bus_message_append_basic(reply, 'b', &dwt);
}

static int disable_while_typing_enabled_by_default(sd_bus *bus, const char *path,
                                                   const char *interface, const char *property,
                                                   sd_bus_message *reply, void *userdata,
                                                   sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t dwt = input->input->default_state.dwt;
    return sd_bus_message_append_basic(reply, 'b', &dwt);
}

static int get_disable_while_typing(sd_bus *bus, const char *path, const char *interface,
                                    const char *property, sd_bus_message *reply, void *userdata,
                                    sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    uint32_t dwt = input->input->state.dwt;
    return sd_bus_message_append_basic(reply, 'b', &dwt);
}

static int set_disable_while_typing(sd_bus *bus, const char *path, const char *interface,
                                    const char *property, sd_bus_message *reply, void *userdata,
                                    sd_bus_error *ret_error)
{
    uint32_t dwt;
    CK(sd_bus_message_read(reply, "b", &dwt));

    struct kde_input *input = userdata;
    struct input_state state = input->input->state;
    state.dwt = dwt;
    input_set_state(input->input, &state);
    return sd_bus_reply_method_return(reply, NULL);
}

static int get_mapped_output(sd_bus *bus, const char *path, const char *interface,
                             const char *property, sd_bus_message *reply, void *userdata,
                             sd_bus_error *ret_error)
{
    struct kde_input *input = userdata;
    const char *mapped_to_output = input->input->state.mapped_to_output;
    return sd_bus_message_append_basic(reply, 's', mapped_to_output ? mapped_to_output : "");
}

static int set_mapped_output(sd_bus *bus, const char *path, const char *interface,
                             const char *property, sd_bus_message *reply, void *userdata,
                             sd_bus_error *ret_error)
{
    const char *output_name = NULL;
    CK(sd_bus_message_read(reply, "s", &output_name));

    bool none_output = !strcmp(output_name, "");
    if (!none_output) {
        struct kywc_output *kywc_output = kywc_output_by_name(output_name);
        if (!kywc_output || !kywc_output->state.enabled) {
            const sd_bus_error error =
                SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild output or disabled.");
            return sd_bus_reply_method_error(reply, &error);
        }
    }

    struct kde_input *input = userdata;
    const char *current = input->input->mapped_output ? input->input->state.mapped_to_output
                                                      : input->input->desired_mapped_output;

    if (input->input->prop.support_mapped_to_output && (!current || strcmp(current, output_name))) {
        struct input_state state = input->input->state;
        state.mapped_to_output = none_output ? NULL : output_name;
        input_set_state(input->input, &state);
    }
    return sd_bus_reply_method_return(reply, NULL);
}

static const sd_bus_vtable input_vtable[] = {
    SD_BUS_VTABLE_START(0),
    KDE_PROP("pointer", "b", is_pointer),
    KDE_PROP("keyboard", "b", is_keyboard),
    KDE_PROP("touchpad", "b", is_touchpad),
    KDE_PROP("touch", "b", is_touch),
    KDE_PROP("tabletTool", "b", is_tablet_tool),
    KDE_PROP("tabletPad", "b", is_tablet_pad),
    KDE_PROP("switchDevice", "b", is_switch),
    KDE_PROP("name", "s", name),
    KDE_PROP("sysName", "s", sys_name),
    KDE_PROP("product", "i", product),
    KDE_PROP("vendor", "i", vendor),
    KDE_PROP("supportsDisableEvents", "b", supports_disable_events),
    KDE_PROP("supportsDisableEventsOnExternalMouse", "b",
             supports_disable_events_on_external_mouse),
    KDE_WPROP("enabled", "b", get_touchpad_enable, set_touchpad_enable),

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

    KDE_PROP("tapFingerCount", "b", tap_finger_count),
    KDE_PROP("tapToClickEnabledByDefault", "b", tap_to_click_enabled_by_default),
    KDE_WPROP("tapToClick", "b", get_tap_click, set_tap_click),

    KDE_PROP("supportsScrollTwoFinger", "b", supports_scroll_two_finger),
    KDE_PROP("scrollTwoFingerEnabledByDefault", "b", scroll_two_finger_enabled_by_default),
    KDE_WPROP("scrollTwoFinger", "b", get_scroll_two_finger, set_scroll_two_finger),

    KDE_PROP("supportsScrollEdge", "b", supports_scroll_edge),
    KDE_PROP("scrollEdgeEnabledByDefault", "b", scroll_edge_enabled_by_default),
    KDE_WPROP("scrollEdge", "b", get_scroll_edge, set_scroll_edge),

    KDE_PROP("supportsDisableWhileTyping", "b", supports_disable_while_typing),
    KDE_PROP("disableWhileTypingEnabledByDefault", "b", disable_while_typing_enabled_by_default),
    KDE_WPROP("disableWhileTyping", "b", get_disable_while_typing, set_disable_while_typing),

    KDE_WPROP("outputName", "s", get_mapped_output, set_mapped_output),

    SD_BUS_VTABLE_END,
};

static int set_keyboard_repeat(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    uint32_t enable, delay, rate;
    sd_bus_message_read(m, "bii", &enable, &rate, &delay);

    struct kde_input_manager *manager = userdata;
    struct kde_input *kde_input;
    wl_list_for_each(kde_input, &manager->inputs, link) {
        struct input *input = kde_input->input;
        if (input->prop.type == WLR_INPUT_DEVICE_KEYBOARD) {
            struct input_state state = input->state;
            state.repeat_delay = enable ? delay : 0;
            state.repeat_rate = enable ? rate : 0;
            input_set_state(input, &state);
        }
    }
    return 1;
}

static const sd_bus_vtable ukui_kwin_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("setKeyboardRepeat", "bii", "", set_keyboard_repeat, 0),
    SD_BUS_VTABLE_END,
};

static void kde_input_notify_destroy(struct kde_input *input)
{
    if (!input->config) {
        return;
    }

    sd_bus *bus = sd_bus_slot_get_bus(input->config->slot);
    sd_bus_emit_signal(bus, service_path, service_interface, "deviceRemoved", "s", input->sys_name);
}

static void kde_input_notify_create(struct kde_input *input)
{
    if (!input->config) {
        return;
    }

    sd_bus *bus = sd_bus_slot_get_bus(input->config->slot);
    sd_bus_emit_signal(bus, service_path, service_interface, "deviceAdded", "s", input->sys_name);
}

static void kde_input_destroy(struct kde_input *input)
{
    kde_input_notify_destroy(input);
    wl_list_remove(&input->link);
    wl_list_remove(&input->destroy.link);
    config_destroy(input->config);
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
    kde_input_notify_create(kde_input);
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

bool kde_input_manager_create(struct config_manager *config_manager)
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
    server_add_destroy_listener(config_manager->server, &kde_input_manager->destroy);

    config_manager_add_config(NULL, "org.ukui.KWin", "/KWin", "org.ukui.KWin", ukui_kwin_vtable,
                              kde_input_manager);

    return true;
}
