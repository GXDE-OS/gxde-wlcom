// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <kywc/log.h>

#include "config.h"
#include "input_p.h"
#include "output.h"

static const char *service_input_path = "/com/kylin/Wlcom/Input";
static const char *service_input_interface = "com.kylin.Wlcom.Input";
static const char *service_seat_path = "/com/kylin/Wlcom/Seat";
static const char *service_seat_interface = "com.kylin.Wlcom.Seat";

static const char *input_type_map[] = {
    [WLR_INPUT_DEVICE_KEYBOARD] = "keyboard",    [WLR_INPUT_DEVICE_POINTER] = "pointer",
    [WLR_INPUT_DEVICE_TOUCH] = "touch",          [WLR_INPUT_DEVICE_TABLET_TOOL] = "table-tool",
    [WLR_INPUT_DEVICE_TABLET_PAD] = "table-pad", [WLR_INPUT_DEVICE_SWITCH] = "switch",
};

void input_prop_and_state_debug(struct input *input)
{
    struct input_prop *prop = &input->prop;
    struct input_state *default_state = &input->default_state;
    struct input_state *state = &input->state;

    kywc_log(KYWC_DEBUG, "input %s(%s) prop debug", input->name, input_type_map[prop->type]);
    kywc_log(KYWC_DEBUG, "\t send_event_modes = %d", prop->send_events_modes);
    kywc_log(KYWC_DEBUG, "\t\t send_events_mode = %d (%d)", state->send_events_mode,
             default_state->send_events_mode);

    if (prop->click_methods != 0) {
        kywc_log(KYWC_DEBUG, "\t click_methods = %d", prop->click_methods);
        kywc_log(KYWC_DEBUG, "\t\t click_method = %d (%d)", state->click_method,
                 default_state->click_method);
    }

    if (prop->tap_finger_count > 0) {
        kywc_log(KYWC_DEBUG, "\t tap_finger_count = %d", prop->tap_finger_count);
        kywc_log(KYWC_DEBUG, "\t\t tap_to_click = %d (%d)", state->tap_to_click,
                 default_state->tap_to_click);
        kywc_log(KYWC_DEBUG, "\t\t tap_button_map = %d (%d)", state->tap_button_map,
                 default_state->tap_button_map);
        kywc_log(KYWC_DEBUG, "\t\t tap_and_drag = %d (%d)", state->tap_and_drag,
                 default_state->tap_and_drag);
        kywc_log(KYWC_DEBUG, "\t\t tap_drag_lock = %d (%d)", state->tap_drag_lock,
                 default_state->tap_drag_lock);
    }

    if (prop->scroll_methods != 0) {
        kywc_log(KYWC_DEBUG, "\t scroll_methods = %d", prop->scroll_methods);
        kywc_log(KYWC_DEBUG, "\t\t scroll_method = %d (%d)", state->scroll_method,
                 default_state->scroll_method);
        if (prop->scroll_methods & 0x4) {
            kywc_log(KYWC_DEBUG, "\t\t scroll_button = %d (%d)", state->scroll_button,
                     default_state->scroll_button);
            kywc_log(KYWC_DEBUG, "\t\t scroll_button_lock = %d (%d)", state->scroll_button_lock,
                     default_state->scroll_button_lock);
        }
    }

    if (prop->has_pointer_accel) {
        kywc_log(KYWC_DEBUG, "\t has_pointer_accel = %d", prop->has_pointer_accel);
        kywc_log(KYWC_DEBUG, "\t\t pointer_accel_speed = %f (%f)", state->pointer_accel_speed,
                 default_state->pointer_accel_speed);
        kywc_log(KYWC_DEBUG, "\t accel_profiles = %d", prop->accel_profiles);
        kywc_log(KYWC_DEBUG, "\t\t accel_profile = %d (%d)", state->accel_profile,
                 default_state->accel_profile);
    }

    if (prop->has_calibration_matrix) {
        kywc_log(KYWC_DEBUG, "\t has_calibration_matrix = %d", prop->has_calibration_matrix);
        kywc_log(KYWC_DEBUG,
                 "\t\t calibration_set_matrix(%f, %f, %f, %f, %f, %f) (%f, %f, %f, %f, %f, %f)",
                 state->calibration_matrix[0], state->calibration_matrix[1],
                 state->calibration_matrix[2], state->calibration_matrix[3],
                 state->calibration_matrix[4], state->calibration_matrix[5],
                 default_state->calibration_matrix[0], default_state->calibration_matrix[1],
                 default_state->calibration_matrix[2], default_state->calibration_matrix[3],
                 default_state->calibration_matrix[4], default_state->calibration_matrix[5]);
    }

    if (prop->has_natural_scroll) {
        kywc_log(KYWC_DEBUG, "\t has_natural_scroll = %d", prop->has_natural_scroll);
        kywc_log(KYWC_DEBUG, "\t\t natural_scroll = %d (%d)", state->natural_scroll,
                 default_state->natural_scroll);
    }

    if (prop->has_left_handed) {
        kywc_log(KYWC_DEBUG, "\t has_left_handed = %d", prop->has_left_handed);
        kywc_log(KYWC_DEBUG, "\t\t left_handed = %d (%d)", state->left_handed,
                 default_state->left_handed);
    }

    if (prop->has_middle_emulation) {
        kywc_log(KYWC_DEBUG, "\t has_middle_emulation = %d", prop->has_middle_emulation);
        kywc_log(KYWC_DEBUG, "\t\t middle_emulation = %d (%d)", state->middle_emulation,
                 state->middle_emulation);
    }

    if (prop->has_dwt) {
        kywc_log(KYWC_DEBUG, "\t has_dwt = %d", prop->has_dwt);
        kywc_log(KYWC_DEBUG, "\t\t dwt = %d (%d)", state->dwt, default_state->dwt);
    }

    if (prop->has_dwtp) {
        kywc_log(KYWC_DEBUG, "\t has_dwtp = %d", prop->has_dwtp);
        kywc_log(KYWC_DEBUG, "\t\t dwtp = %d (%d)", state->dwtp, default_state->dwtp);
    }

    if (prop->has_rotation) {
        kywc_log(KYWC_DEBUG, "\t has_rotation = %d", prop->has_rotation);
        kywc_log(KYWC_DEBUG, "\t\t rotation_angle = %d (%d)", state->rotation_angle,
                 default_state->rotation_angle);
    }
}

static int list_inputs(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct input_manager *manager = userdata;

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(ss)"));

    struct input *input;
    wl_list_for_each(input, &manager->inputs, link) {
        if (input->prop.is_virtual) {
            continue;
        }
        json_object *config = json_object_object_get(manager->config->json, input->name);
        const char *cfg = json_object_to_json_string(config);
        sd_bus_message_append(reply, "(ss)", input->name, cfg);
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static int map_to_output(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *input_name = NULL, *output_name = NULL;
    CK(sd_bus_message_read(m, "ss", &input_name, &output_name));

    struct input *input = input_by_name(input_name);
    if (!input) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild input.");
        return sd_bus_reply_method_error(m, &error);
    }

    bool none_output = !strcmp(output_name, "none");
    if (!none_output) {
        struct kywc_output *kywc_output = kywc_output_by_name(output_name);
        if (!kywc_output || !kywc_output->state.enabled) {
            const sd_bus_error error =
                SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild output or disabled.");
            return sd_bus_reply_method_error(m, &error);
        }
    }

    const char *current =
        input->mapped_output ? input->state.mapped_to_output : input->desired_mapped_output;
    if (input->prop.support_mapped_to_output && (!current || strcmp(current, output_name))) {
        struct input_state state = input->state;
        state.mapped_to_output = none_output ? NULL : output_name;
        input_set_state(input, &state);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int change_seat(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *input_name = NULL, *seat_name = NULL;
    CK(sd_bus_message_read(m, "ss", &input_name, &seat_name));

    struct input *input = input_by_name(input_name);
    if (!input) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild input.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (strncmp(seat_name, "seat", 4)) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild seat.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (strcmp(input->state.seat, seat_name)) {
        struct input_state state = input->state;
        state.seat = seat_name;
        input_set_state(input, &state);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int set_send_events(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *input_name = NULL;
    uint32_t mode = 0;
    CK(sd_bus_message_read(m, "su", &input_name, &mode));

    struct input *input = input_by_name(input_name);
    if (!input) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild input.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (input->prop.send_events_modes < mode) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild mode.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (input->state.send_events_mode != mode) {
        struct input_state state = input->state;
        state.send_events_mode = mode;
        input_set_state(input, &state);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int enable_tap_to_click(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *input_name = NULL;
    int32_t enabled = 0;
    CK(sd_bus_message_read(m, "sb", &input_name, &enabled));

    struct input *input = input_by_name(input_name);
    if (!input) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild input.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (input->prop.tap_finger_count && input->state.tap_to_click != enabled) {
        struct input_state state = input->state;
        state.tap_to_click = enabled;
        input_set_state(input, &state);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int set_pointer_speed(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *input_name = NULL;
    double speed = 0.0f;
    CK(sd_bus_message_read(m, "sd", &input_name, &speed));

    struct input *input = input_by_name(input_name);
    if (!input) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild input.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (speed < -1.0f || speed > 1.0f) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild speed.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (input->prop.has_pointer_accel && input->state.pointer_accel_speed != speed) {
        struct input_state state = input->state;
        state.pointer_accel_speed = speed;
        input_set_state(input, &state);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int set_accel_profile(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *input_name = NULL;
    uint32_t accel_profile;
    CK(sd_bus_message_read(m, "su", &input_name, &accel_profile));

    struct input *input = input_by_name(input_name);
    if (!input) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild input.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (input->prop.has_pointer_accel && input->prop.accel_profiles &&
        input->state.accel_profile != accel_profile) {
        struct input_state state = input->state;
        state.accel_profile = accel_profile;
        input_set_state(input, &state);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int set_scroll_method(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *input_name = NULL;
    uint32_t scroll_method;
    CK(sd_bus_message_read(m, "su", &input_name, &scroll_method));

    struct input *input = input_by_name(input_name);
    if (!input) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild input.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (input->prop.scroll_methods && input->state.scroll_method != scroll_method) {
        struct input_state state = input->state;
        state.scroll_method = scroll_method;
        input_set_state(input, &state);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int set_disable_while_typing(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *input_name = NULL;
    uint32_t dwt;
    CK(sd_bus_message_read(m, "sb", &input_name, &dwt));

    struct input *input = input_by_name(input_name);
    if (!input) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild input.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (input->prop.has_dwt && input->state.dwt != dwt) {
        struct input_state state = input->state;
        state.dwt = dwt;
        input_set_state(input, &state);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int enable_natural_scroll(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *input_name = NULL;
    int32_t enabled = 0;
    CK(sd_bus_message_read(m, "sb", &input_name, &enabled));

    struct input *input = input_by_name(input_name);
    if (!input) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild input.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (input->prop.has_natural_scroll && input->state.natural_scroll != enabled) {
        struct input_state state = input->state;
        state.natural_scroll = enabled;
        input_set_state(input, &state);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int enable_left_handed(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *input_name = NULL;
    int32_t enabled = 0;
    CK(sd_bus_message_read(m, "sb", &input_name, &enabled));

    struct input *input = input_by_name(input_name);
    if (!input) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild input.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (input->prop.has_left_handed && input->state.left_handed != enabled) {
        struct input_state state = input->state;
        state.left_handed = enabled;
        input_set_state(input, &state);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int set_repeat_info(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *input_name = NULL;
    int32_t rate, delay;
    CK(sd_bus_message_read(m, "sii", &input_name, &rate, &delay));

    struct input *input = input_by_name(input_name);
    if (!input) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild input.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (input->prop.type == WLR_INPUT_DEVICE_KEYBOARD &&
        (input->state.repeat_rate != rate || input->state.repeat_delay != delay)) {
        struct input_state state = input->state;
        state.repeat_rate = rate;
        state.repeat_delay = delay;
        input_set_state(input, &state);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int list_seats(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct input_manager *manager = userdata;

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(ss)"));

    struct seat *seat;
    wl_list_for_each(seat, &manager->seats, link) {
        json_object *config = json_object_object_get(manager->seat_config->json, seat->name);
        const char *cfg = json_object_to_json_string(config);
        sd_bus_message_append(reply, "(ss)", seat->name, cfg);
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static int set_cursor(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *seat_name, *cursor_theme;
    uint32_t cursor_size;
    CK(sd_bus_message_read(m, "ssu", &seat_name, &cursor_theme, &cursor_size));

    struct seat *seat = seat_by_name(seat_name);
    if (!seat) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild seat.");
        return sd_bus_reply_method_error(m, &error);
    }

    cursor_set_xcursor_manager(seat->cursor, cursor_theme, cursor_size, true);
    seat_write_config(seat);

    return sd_bus_reply_method_return(m, NULL);
}

static int set_scroll_factor(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *seat_name;
    double scroll_factor;
    CK(sd_bus_message_read(m, "sd", &seat_name, &scroll_factor));

    struct seat *seat = seat_by_name(seat_name);
    if (!seat) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild seat.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (scroll_factor <= 0) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild scroll_factor.");
        return sd_bus_reply_method_error(m, &error);
    }

    seat->state.scroll_factor = scroll_factor;

    seat_write_config(seat);

    return sd_bus_reply_method_return(m, NULL);
}

static int set_double_click_time(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *seat_name;
    uint32_t double_click_time;
    CK(sd_bus_message_read(m, "su", &seat_name, &double_click_time));

    struct seat *seat = seat_by_name(seat_name);
    if (!seat) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invaild seat.");
        return sd_bus_reply_method_error(m, &error);
    }

    seat->state.double_click_time = double_click_time;

    seat_write_config(seat);

    return sd_bus_reply_method_return(m, NULL);
}

static const sd_bus_vtable service_input_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListAllInputs", "", "a(ss)", list_inputs, 0),
    SD_BUS_METHOD("MapToOutput", "ss", "", map_to_output, 0),
    SD_BUS_METHOD("ChangeSeat", "ss", "", change_seat, 0),
    SD_BUS_METHOD("SetSendEventsMode", "su", "", set_send_events, 0),
    SD_BUS_METHOD("EnableTapToClick", "sb", "", enable_tap_to_click, 0),
    SD_BUS_METHOD("SetPointerSpeed", "sd", "", set_pointer_speed, 0),
    SD_BUS_METHOD("SetAccelProfile", "su", "", set_accel_profile, 0),
    SD_BUS_METHOD("SetScrollMethod", "su", "", set_scroll_method, 0),
    SD_BUS_METHOD("SetDisableWhileTyping", "sb", "", set_disable_while_typing, 0),
    SD_BUS_METHOD("EnableNaturalScroll", "sb", "", enable_natural_scroll, 0),
    SD_BUS_METHOD("EnableLeftHand", "sb", "", enable_left_handed, 0),
    SD_BUS_METHOD("SetRepeatInfo", "sii", "", set_repeat_info, 0),
    SD_BUS_VTABLE_END,
};

static const sd_bus_vtable service_seat_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListAllSeats", "", "a(ss)", list_seats, 0),
    SD_BUS_METHOD("SetCursor", "ssu", "", set_cursor, 0),
    SD_BUS_METHOD("SetScrollFactor", "sd", "", set_scroll_factor, 0),
    SD_BUS_METHOD("SetDoubleClickTime", "su", "", set_double_click_time, 0),
    SD_BUS_VTABLE_END,
};

bool input_manager_config_init(struct input_manager *input_manager)
{
    input_manager->config =
        config_manager_add_config("Inputs", NULL, service_input_path, service_input_interface,
                                  service_input_vtable, input_manager);
    input_manager->seat_config =
        config_manager_add_config("Seats", NULL, service_seat_path, service_seat_interface,
                                  service_seat_vtable, input_manager);

    return !!input_manager->config && !!input_manager->seat_config;
}

bool input_read_config(struct input *input, struct input_state *state)
{
    struct input_manager *manager = input->manager;
    if (!manager->config || !manager->config->json) {
        return false;
    }

    json_object *config = json_object_object_get(manager->config->json, input->name);
    if (!config) {
        return false;
    }

    json_object *data;
    if (json_object_object_get_ex(config, "mapped_to_output", &data)) {
        state->mapped_to_output = json_object_get_string(data);
    }
    if (json_object_object_get_ex(config, "seat", &data)) {
        state->seat = json_object_get_string(data);
    }

    if (json_object_object_get_ex(config, "send_events_mode", &data)) {
        state->send_events_mode = json_object_get_int(data);
    }

    if (input->prop.tap_finger_count > 0) {
        if (json_object_object_get_ex(config, "tap_to_click", &data)) {
            state->tap_to_click = json_object_get_boolean(data);
        }
        if (json_object_object_get_ex(config, "tap_button_map", &data)) {
            state->tap_button_map = json_object_get_int(data);
        }
        if (json_object_object_get_ex(config, "tap_and_drag", &data)) {
            state->tap_and_drag = json_object_get_boolean(data);
        }
        if (json_object_object_get_ex(config, "tap_drag_lock", &data)) {
            state->tap_drag_lock = json_object_get_boolean(data);
        }
    }

    if (input->prop.has_natural_scroll) {
        if (json_object_object_get_ex(config, "natural_scroll", &data)) {
            state->natural_scroll = json_object_get_boolean(data);
        }
    }

    if (input->prop.has_middle_emulation) {
        if (json_object_object_get_ex(config, "middle_emulation", &data)) {
            state->middle_emulation = json_object_get_boolean(data);
        }
    }

    if (input->prop.has_left_handed) {
        if (json_object_object_get_ex(config, "left_handed", &data)) {
            state->left_handed = json_object_get_boolean(data);
        }
    }

    if (input->prop.has_dwt) {
        if (json_object_object_get_ex(config, "dwt", &data)) {
            state->dwt = json_object_get_boolean(data);
        }
    }

    if (input->prop.has_dwtp) {
        if (json_object_object_get_ex(config, "dwtp", &data)) {
            state->dwtp = json_object_get_boolean(data);
        }
    }

    if (input->prop.scroll_methods) {
        if (json_object_object_get_ex(config, "scroll_method", &data)) {
            state->scroll_method = json_object_get_int(data);
        }
        if (input->prop.scroll_methods & 0x4) {
            if (json_object_object_get_ex(config, "scroll_button", &data)) {
                state->scroll_button = json_object_get_int(data);
            }
            if (json_object_object_get_ex(config, "scroll_button_lock", &data)) {
                state->scroll_button_lock = json_object_get_boolean(data);
            }
        }
    }

    if (input->prop.click_methods) {
        if (json_object_object_get_ex(config, "click_method", &data)) {
            state->click_method = json_object_get_int(data);
        }
    }

    if (input->prop.has_pointer_accel) {
        if (json_object_object_get_ex(config, "pointer_accel_speed", &data)) {
            state->pointer_accel_speed = json_object_get_double(data);
        }
        if (input->prop.accel_profiles) {
            if (json_object_object_get_ex(config, "accel_profile", &data)) {
                state->accel_profile = json_object_get_int(data);
            }
        }
    }

    if (input->prop.has_calibration_matrix) {
        if (json_object_object_get_ex(config, "calibration_matrix", &data)) {
            for (int i = 0; i < 6; i++) {
                state->calibration_matrix[i] =
                    json_object_get_double(json_object_array_get_idx(data, i));
            }
        }
    }

    if (input->prop.type == WLR_INPUT_DEVICE_KEYBOARD) {
        if (json_object_object_get_ex(config, "repeat_delay", &data)) {
            state->repeat_delay = json_object_get_int(data);
        }
        if (json_object_object_get_ex(config, "repeat_rate", &data)) {
            state->repeat_rate = json_object_get_int(data);
        }
    }

    return true;
}

#define WRITE_CONFIG(entry, type)                                                                  \
    if (state->entry == default_state->entry) {                                                    \
        json_object_object_del(config, #entry);                                                    \
    } else {                                                                                       \
        json_object_object_add(config, #entry, json_object_new_##type(state->entry));              \
    }

void input_write_config(struct input *input)
{
    struct input_manager *manager = input->manager;
    if (!manager->config || !manager->config->json) {
        return;
    }

    struct input_state *state = &input->state;
    json_object *config = json_object_object_get(manager->config->json, input->name);
    if (!config) {
        config = json_object_new_object();
        json_object_object_add(manager->config->json, input->name, config);
    }

    if (state->mapped_to_output) {
        json_object_object_add(config, "mapped_to_output",
                               json_object_new_string(state->mapped_to_output));
    } else if (input->desired_mapped_output) {
        json_object_object_add(config, "mapped_to_output",
                               json_object_new_string(input->desired_mapped_output));
    } else {
        json_object_object_del(config, "mapped_to_output");
    }

    if (state->seat && strcmp(state->seat, "seat0")) {
        json_object_object_add(config, "seat", json_object_new_string(state->seat));
    } else {
        json_object_object_del(config, "seat");
    }

    /* filter by default state */
    struct input_state *default_state = &input->default_state;
    WRITE_CONFIG(send_events_mode, int);

    if (input->prop.tap_finger_count > 0) {
        WRITE_CONFIG(tap_to_click, boolean);
        WRITE_CONFIG(tap_button_map, int);
        WRITE_CONFIG(tap_and_drag, boolean);
        WRITE_CONFIG(tap_drag_lock, boolean);
    }

    if (input->prop.has_natural_scroll) {
        WRITE_CONFIG(natural_scroll, boolean);
    }

    if (input->prop.has_middle_emulation) {
        WRITE_CONFIG(middle_emulation, boolean);
    }

    if (input->prop.has_left_handed) {
        WRITE_CONFIG(left_handed, boolean);
    }

    if (input->prop.has_dwt) {
        WRITE_CONFIG(dwt, boolean);
    }

    if (input->prop.has_dwtp) {
        WRITE_CONFIG(dwtp, boolean);
    }

    if (input->prop.scroll_methods) {
        WRITE_CONFIG(scroll_method, int);
        if (input->prop.scroll_methods & 0x4) {
            WRITE_CONFIG(scroll_button, int);
            WRITE_CONFIG(scroll_button_lock, boolean);
        }
    }

    if (input->prop.click_methods) {
        WRITE_CONFIG(click_method, int);
    }

    if (input->prop.has_pointer_accel) {
        WRITE_CONFIG(pointer_accel_speed, double);
        if (input->prop.accel_profiles) {
            WRITE_CONFIG(accel_profile, int);
        }
    }

    if (input->prop.has_calibration_matrix) {
        if (memcmp(state->calibration_matrix, default_state->calibration_matrix,
                   sizeof(state->calibration_matrix)) == 0) {
            json_object_object_del(config, "calibration_matrix");
        } else {
            json_object *matrix = json_object_new_array_ext(6);
            for (int i = 0; i < 6; i++) {
                json_object_array_add(matrix, json_object_new_double(state->calibration_matrix[i]));
            }
            json_object_object_add(config, "calibration_matrix", matrix);
        }
    }

    // TODO: rotation angle

    if (input->prop.type == WLR_INPUT_DEVICE_KEYBOARD) {
        WRITE_CONFIG(repeat_delay, int);
        WRITE_CONFIG(repeat_rate, int);
    }
}

#undef WRITE_CONFIG

bool seat_read_config(struct seat *seat)
{
    if (!seat->manager->seat_config || !seat->manager->seat_config->json) {
        return false;
    }

    json_object *config = json_object_object_get(seat->manager->seat_config->json, seat->name);
    if (!config) {
        return true;
    }

    json_object *data;
    if (json_object_object_get_ex(config, "cursor_theme", &data)) {
        seat->state.cursor_theme = json_object_get_string(data);
    }
    if (json_object_object_get_ex(config, "cursor_size", &data)) {
        seat->state.cursor_size = json_object_get_int(data);
    }
    if (json_object_object_get_ex(config, "scroll_factor", &data)) {
        seat->state.scroll_factor = json_object_get_double(data);
    }
    if (json_object_object_get_ex(config, "double_click_time", &data)) {
        seat->state.double_click_time = json_object_get_double(data);
    }

    return true;
}

void seat_write_config(struct seat *seat)
{
    if (!seat->manager->seat_config) {
        return;
    }

    json_object *config = json_object_object_get(seat->manager->seat_config->json, seat->name);
    if (!config) {
        config = json_object_new_object();
        json_object_object_add(seat->manager->seat_config->json, seat->name, config);
    }

    if (seat->state.cursor_theme && strcmp(seat->state.cursor_theme, "default")) {
        json_object_object_add(config, "cursor_theme",
                               json_object_new_string(seat->state.cursor_theme));
    } else {
        json_object_object_del(config, "cursor_theme");
    }

    if (seat->state.cursor_size != 24) {
        json_object_object_add(config, "cursor_size", json_object_new_int(seat->state.cursor_size));
    } else {
        json_object_object_del(config, "cursor_size");
    }

    if (seat->state.scroll_factor != 1.0) {
        json_object_object_add(config, "scroll_factor",
                               json_object_new_double(seat->state.scroll_factor));
    } else {
        json_object_object_del(config, "scroll_factor");
    }

    if (seat->state.double_click_time != 500) {
        json_object_object_add(config, "double_click_time",
                               json_object_new_double(seat->state.double_click_time));
    } else {
        json_object_object_del(config, "double_click_time");
    }
}
