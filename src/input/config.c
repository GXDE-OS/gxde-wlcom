#include <kywc/log.h>

#include "config.h"
#include "input.h"

static const char *service_path = "/com/kylin/Wlcom/Input";
static const char *service_interface = "com.kylin.Wlcom.Input";

static const char *input_type_map[] = {
    [KYWC_INPUT_DEVICE_KEYBOARD] = "keyboard",    [KYWC_INPUT_DEVICE_POINTER] = "pointer",
    [KYWC_INPUT_DEVICE_TOUCH] = "touch",          [KYWC_INPUT_DEVICE_TABLET_TOOL] = "table-tool",
    [KYWC_INPUT_DEVICE_TABLET_PAD] = "table-pad", [KYWC_INPUT_DEVICE_SWITCH] = "switch",
};

void input_prop_and_state_debug(struct input *input)
{
    struct input_prop *prop = &input->prop;
    struct input_state *state = &input->state;

    kywc_log(KYWC_DEBUG, "input %s(%s) prop debug", input->name, input_type_map[prop->type]);
    kywc_log(KYWC_DEBUG, "\t send_event_modes = %d", prop->send_events_modes);
    kywc_log(KYWC_DEBUG, "\t\t send_events_mode = %d", state->send_events_mode);

    if (prop->click_methods != 0) {
        kywc_log(KYWC_DEBUG, "\t click_methods = %d", prop->click_methods);
        kywc_log(KYWC_DEBUG, "\t\t click_method = %d", state->click_method);
    }

    if (prop->tap_finger_count > 0) {
        kywc_log(KYWC_DEBUG, "\t tap_finger_count = %d", prop->tap_finger_count);
        kywc_log(KYWC_DEBUG, "\t\t tap_to_click = %d", state->tap_to_click);
        kywc_log(KYWC_DEBUG, "\t\t tap_button_map = %d", state->tap_button_map);
        kywc_log(KYWC_DEBUG, "\t\t tap_and_drag = %d", state->tap_and_drag);
        kywc_log(KYWC_DEBUG, "\t\t tap_drag_lock = %d", state->tap_drag_lock);
    }

    if (prop->scroll_methods != 0) {
        kywc_log(KYWC_DEBUG, "\t scroll_methods = %d", prop->scroll_methods);
        kywc_log(KYWC_DEBUG, "\t\t scroll_method = %d", state->scroll_method);
        if (prop->scroll_methods & 0x4) {
            kywc_log(KYWC_DEBUG, "\t\t scroll_button = %d", state->scroll_button);
            kywc_log(KYWC_DEBUG, "\t\t scroll_button_lock = %d", state->scroll_button_lock);
        }
    }

    if (prop->has_pointer_accel) {
        kywc_log(KYWC_DEBUG, "\t has_pointer_accel = %d", prop->has_pointer_accel);
        kywc_log(KYWC_DEBUG, "\t\t pointer_accel_speed = %f", state->pointer_accel_speed);
        kywc_log(KYWC_DEBUG, "\t accel_profiles = %d", prop->accel_profiles);
        kywc_log(KYWC_DEBUG, "\t\t accel_profile = %d", state->accel_profile);
    }

    if (prop->has_calibration_matrix) {
        kywc_log(KYWC_DEBUG, "\t has_calibration_matrix = %d", prop->has_calibration_matrix);
        kywc_log(KYWC_DEBUG, "\t\t calibration_set_matrix(%f, %f, %f, %f, %f, %f)",
                 state->calibration_matrix[0], state->calibration_matrix[1],
                 state->calibration_matrix[2], state->calibration_matrix[3],
                 state->calibration_matrix[4], state->calibration_matrix[5]);
    }

    if (prop->has_natural_scroll) {
        kywc_log(KYWC_DEBUG, "\t has_natural_scroll = %d", prop->has_natural_scroll);
        kywc_log(KYWC_DEBUG, "\t\t natural_scroll = %d", state->natural_scroll);
    }

    if (prop->has_left_handed) {
        kywc_log(KYWC_DEBUG, "\t has_left_handed = %d", prop->has_left_handed);
        kywc_log(KYWC_DEBUG, "\t\t left_handed = %d", state->left_handed);
    }

    if (prop->has_middle_emulation) {
        kywc_log(KYWC_DEBUG, "\t has_middle_emulation = %d", prop->has_middle_emulation);
        kywc_log(KYWC_DEBUG, "\t\t middle_emulation = %d", state->middle_emulation);
    }

    if (prop->has_dwt) {
        kywc_log(KYWC_DEBUG, "\t has_dwt = %d", prop->has_dwt);
        kywc_log(KYWC_DEBUG, "\t\t dwt = %d", state->dwt);
    }

    if (prop->has_dwtp) {
        kywc_log(KYWC_DEBUG, "\t has_dwtp = %d", prop->has_dwtp);
        kywc_log(KYWC_DEBUG, "\t\t dwtp = %d", state->dwtp);
    }

    if (prop->has_rotation) {
        kywc_log(KYWC_DEBUG, "\t has_rotation = %d", prop->has_rotation);
        kywc_log(KYWC_DEBUG, "\t\t rotation_angle = %d", state->rotation_angle);
    }
}

static int list_inputs(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct input_manager *manager = userdata;

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "s"));

    struct input *input;
    wl_list_for_each(input, &manager->inputs, link) {
        if (input->prop.is_virtual) {
            continue;
        }
        sd_bus_message_append_basic(reply, 's', input->name);
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 0;
}

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListAllInputs", "", "as", list_inputs, 0),
    SD_BUS_VTABLE_END,
};

bool input_manager_config_init(struct input_manager *input_manager)
{
    input_manager->config = config_manager_add_config("Inputs", service_path, service_interface,
                                                      service_vtable, input_manager);
    return !!input_manager->config;
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

    if (input->prop.type == KYWC_INPUT_DEVICE_KEYBOARD) {
        if (json_object_object_get_ex(config, "repeat_delay", &data)) {
            state->repeat_delay = json_object_get_int(data);
        }
        if (json_object_object_get_ex(config, "repeat_rate", &data)) {
            state->repeat_rate = json_object_get_int(data);
        }
    }

    return true;
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
    }
    if (state->seat) {
        json_object_object_add(config, "seat", json_object_new_string(state->seat));
    }

    json_object_object_add(config, "send_events_mode",
                           json_object_new_int(state->send_events_mode));

    if (input->prop.tap_finger_count > 0) {
        json_object_object_add(config, "tap_to_click",
                               json_object_new_boolean(state->tap_to_click));
        json_object_object_add(config, "tap_button_map",
                               json_object_new_int(state->tap_button_map));
        json_object_object_add(config, "tap_and_drag",
                               json_object_new_boolean(state->tap_and_drag));
        json_object_object_add(config, "tap_drag_lock",
                               json_object_new_boolean(state->tap_drag_lock));
    }

    if (input->prop.has_natural_scroll) {
        json_object_object_add(config, "natural_scroll",
                               json_object_new_boolean(state->natural_scroll));
    }

    if (input->prop.has_middle_emulation) {
        json_object_object_add(config, "middle_emulation",
                               json_object_new_boolean(state->middle_emulation));
    }

    if (input->prop.has_left_handed) {
        json_object_object_add(config, "left_handed", json_object_new_boolean(state->left_handed));
    }

    if (input->prop.has_dwt) {
        json_object_object_add(config, "dwt", json_object_new_boolean(state->dwt));
    }

    if (input->prop.has_dwtp) {
        json_object_object_add(config, "dwtp", json_object_new_boolean(state->dwtp));
    }

    if (input->prop.scroll_methods) {
        json_object_object_add(config, "scroll_method", json_object_new_int(state->scroll_method));
        if (input->prop.scroll_methods & 0x4) {
            json_object_object_add(config, "scroll_button",
                                   json_object_new_int(state->scroll_button));
            json_object_object_add(config, "scroll_button_lock",
                                   json_object_new_boolean(state->scroll_button_lock));
        }
    }

    if (input->prop.click_methods) {
        json_object_object_add(config, "click_method", json_object_new_int(state->click_method));
    }

    if (input->prop.has_pointer_accel) {
        json_object_object_add(config, "pointer_accel_speed",
                               json_object_new_double(state->pointer_accel_speed));
        if (input->prop.accel_profiles) {
            json_object_object_add(config, "accel_profile",
                                   json_object_new_int(state->accel_profile));
        }
    }

    if (input->prop.has_calibration_matrix) {
        json_object *matrix = json_object_new_array_ext(6);
        for (int i = 0; i < 6; i++) {
            json_object_array_add(matrix, json_object_new_double(state->calibration_matrix[i]));
        }
        json_object_object_add(config, "calibration_matrix", matrix);
    }

    // TODO: rotation angle

    if (input->prop.type == KYWC_INPUT_DEVICE_KEYBOARD) {
        json_object_object_add(config, "repeat_delay", json_object_new_int(state->repeat_delay));
        json_object_object_add(config, "repeat_rate", json_object_new_int(state->repeat_rate));
    }
}
