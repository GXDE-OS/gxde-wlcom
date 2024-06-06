// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <libinput.h>

#include <kywc/log.h>

#include "input_p.h"

void libinput_get_prop(struct input *input, struct input_prop *prop)
{
    struct libinput_device *device = input->device;
    if (!device) {
        kywc_log(KYWC_DEBUG, "input %s is not a libinput device", input->name);
        return;
    }

    prop->send_events_modes = libinput_device_config_send_events_get_modes(device);
    prop->click_methods = libinput_device_config_click_get_methods(device);
    prop->scroll_methods = libinput_device_config_scroll_get_methods(device);
    prop->accel_profiles = libinput_device_config_accel_get_profiles(device);

    prop->tap_finger_count = libinput_device_config_tap_get_finger_count(device);

    prop->has_calibration_matrix = libinput_device_config_calibration_has_matrix(device);
    prop->has_pointer_accel = libinput_device_config_accel_is_available(device);
    prop->has_natural_scroll = libinput_device_config_scroll_has_natural_scroll(device);
    prop->has_left_handed = libinput_device_config_left_handed_is_available(device);
    prop->has_middle_emulation = libinput_device_config_middle_emulation_is_available(device);
    prop->has_dwt = libinput_device_config_dwt_is_available(device);
    prop->has_dwtp = libinput_device_config_dwtp_is_available(device);
    prop->has_rotation = libinput_device_config_rotation_is_available(device);
}

void libinput_get_state(struct input *input, struct input_state *state)
{
    struct libinput_device *device = input->device;
    if (!device) {
        kywc_log(KYWC_DEBUG, "input %s is not a libinput device", input->name);
        return;
    }

    struct input_prop *prop = &input->prop;

    state->send_events_mode = libinput_device_config_send_events_get_mode(device);
    state->click_method = libinput_device_config_click_get_method(device);

    if (prop->tap_finger_count > 0) {
        state->tap_to_click = libinput_device_config_tap_get_enabled(device);
        state->tap_button_map = libinput_device_config_tap_get_button_map(device);
        state->tap_and_drag = libinput_device_config_tap_get_drag_enabled(device);
        state->tap_drag_lock = libinput_device_config_tap_get_drag_lock_enabled(device);
    }

    if (prop->has_calibration_matrix) {
        libinput_device_config_calibration_get_matrix(device, state->calibration_matrix);
    }

    if (prop->has_pointer_accel) {
        state->pointer_accel_speed = libinput_device_config_accel_get_speed(device);
        state->accel_profile = libinput_device_config_accel_get_profile(device);
    }

    if (prop->has_natural_scroll) {
        state->natural_scroll = libinput_device_config_scroll_get_natural_scroll_enabled(device);
    }

    if (prop->has_left_handed) {
        state->left_handed = libinput_device_config_left_handed_get(device);
    }

    if (prop->has_middle_emulation) {
        state->middle_emulation = libinput_device_config_middle_emulation_get_enabled(device);
    }

    state->scroll_method = libinput_device_config_scroll_get_method(device);
    if (state->scroll_method == LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN) {
        state->scroll_button = libinput_device_config_scroll_get_button(device);
        state->scroll_button_lock = libinput_device_config_scroll_get_button_lock(device);
    }

    if (prop->has_dwt) {
        state->dwt = libinput_device_config_dwt_get_enabled(device);
    }

    if (prop->has_dwtp) {
        state->dwtp = libinput_device_config_dwtp_get_enabled(device);
    }

    if (prop->has_rotation) {
        state->rotation_angle = libinput_device_config_rotation_get_angle(device);
    }
}

void libinput_get_default_state(struct input *input, struct input_state *state)
{
    struct libinput_device *device = input->device;
    if (!device) {
        kywc_log(KYWC_DEBUG, "input %s is not a libinput device", input->name);
        return;
    }

    struct input_prop *prop = &input->prop;

    state->send_events_mode = libinput_device_config_send_events_get_default_mode(device);
    state->click_method = libinput_device_config_click_get_default_method(device);

    if (prop->tap_finger_count > 0) {
        state->tap_to_click = libinput_device_config_tap_get_default_enabled(device);
        state->tap_button_map = libinput_device_config_tap_get_default_button_map(device);
        state->tap_and_drag = libinput_device_config_tap_get_default_drag_enabled(device);
        state->tap_drag_lock = libinput_device_config_tap_get_default_drag_lock_enabled(device);
    }

    if (prop->has_calibration_matrix) {
        libinput_device_config_calibration_get_default_matrix(device, state->calibration_matrix);
    }

    if (prop->has_pointer_accel) {
        state->pointer_accel_speed = libinput_device_config_accel_get_default_speed(device);
        state->accel_profile = libinput_device_config_accel_get_default_profile(device);
    }

    if (prop->has_natural_scroll) {
        state->natural_scroll =
            libinput_device_config_scroll_get_default_natural_scroll_enabled(device);
    }

    if (prop->has_left_handed) {
        state->left_handed = libinput_device_config_left_handed_get_default(device);
    }

    if (prop->has_middle_emulation) {
        state->middle_emulation =
            libinput_device_config_middle_emulation_get_default_enabled(device);
    }

    state->scroll_method = libinput_device_config_scroll_get_default_method(device);
    if (state->scroll_method == LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN) {
        state->scroll_button = libinput_device_config_scroll_get_default_button(device);
        state->scroll_button_lock = libinput_device_config_scroll_get_default_button_lock(device);
    }

    if (prop->has_dwt) {
        state->dwt = libinput_device_config_dwt_get_default_enabled(device);
    }

    if (prop->has_dwtp) {
        state->dwtp = libinput_device_config_dwtp_get_default_enabled(device);
    }

    if (prop->has_rotation) {
        state->rotation_angle = libinput_device_config_rotation_get_default_angle(device);
    }
}

static bool log_status(enum libinput_config_status status)
{
    if (status != LIBINPUT_CONFIG_STATUS_SUCCESS) {
        kywc_log(KYWC_ERROR, "Failed to apply libinput config: %s",
                 libinput_config_status_to_str(status));
        return false;
    }

    return true;
}

bool libinput_set_state(struct input *input, struct input_state *state)
{
    struct libinput_device *device = input->device;
    if (!device) {
        kywc_log(KYWC_DEBUG, "input %s is not a libinput device", input->name);
        return false;
    }

    struct input_state *current = &input->state;
    bool sucess = true;

    if (current->send_events_mode != state->send_events_mode) {
        kywc_log(KYWC_DEBUG, "send_events_set_mode(%d)", state->send_events_mode);
        sucess &= log_status(
            libinput_device_config_send_events_set_mode(device, state->send_events_mode));
    }

    if (current->click_method != state->click_method) {
        kywc_log(KYWC_DEBUG, "click_set_method(%d)", state->click_method);
        sucess &= log_status(libinput_device_config_click_set_method(device, state->click_method));
    }

    if (input->prop.tap_finger_count > 0) {
        if (current->tap_to_click != state->tap_to_click) {
            kywc_log(KYWC_DEBUG, "tap_set_enabled(%d)", state->tap_to_click);
            sucess &=
                log_status(libinput_device_config_tap_set_enabled(device, state->tap_to_click));
        }

        // XXX: skip others ?
        // if (!state->tap_to_click) {
        //  }

        if (current->tap_and_drag != state->tap_and_drag) {
            kywc_log(KYWC_DEBUG, "tap_set_drag_enabled(%d)", state->tap_and_drag);
            sucess &= log_status(
                libinput_device_config_tap_set_drag_enabled(device, state->tap_and_drag));
        }
        if (current->tap_drag_lock != state->tap_drag_lock) {
            kywc_log(KYWC_DEBUG, "tap_set_drag_lock_enabled(%d)", state->tap_drag_lock);
            sucess &= log_status(
                libinput_device_config_tap_set_drag_lock_enabled(device, state->tap_drag_lock));
        }
        if (current->tap_button_map != state->tap_button_map) {
            kywc_log(KYWC_DEBUG, "tap_set_button_map(%d)", state->tap_button_map);
            sucess &= log_status(
                libinput_device_config_tap_set_button_map(device, state->tap_button_map));
        }
    }

    if (input->prop.has_calibration_matrix) {
        bool changed = false;
        for (int i = 0; i < 6; i++) {
            if (current->calibration_matrix[i] != state->calibration_matrix[i]) {
                changed = true;
                break;
            }
        }
        if (changed) {
            kywc_log(KYWC_DEBUG, "calibration_set_matrix(%f, %f, %f, %f, %f, %f)",
                     state->calibration_matrix[0], state->calibration_matrix[1],
                     state->calibration_matrix[2], state->calibration_matrix[3],
                     state->calibration_matrix[4], state->calibration_matrix[5]);
            sucess &= log_status(
                libinput_device_config_calibration_set_matrix(device, state->calibration_matrix));
        }
    }

    if (input->prop.has_pointer_accel) {
        if (current->pointer_accel_speed != state->pointer_accel_speed) {
            kywc_log(KYWC_DEBUG, "accel_set_speed(%f)", state->pointer_accel_speed);
            sucess &= log_status(
                libinput_device_config_accel_set_speed(device, state->pointer_accel_speed));
        }
        if (current->accel_profile != state->accel_profile) {
            kywc_log(KYWC_DEBUG, "accel_set_profile(%d)", state->accel_profile);
            sucess &=
                log_status(libinput_device_config_accel_set_profile(device, state->accel_profile));
        }
    }

    if (input->prop.has_natural_scroll && current->natural_scroll != state->natural_scroll) {
        kywc_log(KYWC_DEBUG, "scroll_set_natural_scroll_enabled(%d)", state->natural_scroll);
        sucess &= log_status(libinput_device_config_scroll_set_natural_scroll_enabled(
            device, state->natural_scroll));
    }

    if (input->prop.has_left_handed && current->left_handed != state->left_handed) {
        kywc_log(KYWC_DEBUG, "left_handed_set(%d)", state->left_handed);
        sucess &= log_status(libinput_device_config_left_handed_set(device, state->left_handed));
    }

    if (input->prop.has_middle_emulation && current->middle_emulation != state->middle_emulation) {
        kywc_log(KYWC_DEBUG, "middle_emulation_set_enabled(%d)", state->middle_emulation);
        sucess &= log_status(
            libinput_device_config_middle_emulation_set_enabled(device, state->middle_emulation));
    }

    if (current->scroll_method != state->scroll_method) {
        kywc_log(KYWC_DEBUG, "scroll_set_method(%d)", state->scroll_method);
        sucess &=
            log_status(libinput_device_config_scroll_set_method(device, state->scroll_method));
    }
    if (state->scroll_method == LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN) {
        if (current->scroll_button != state->scroll_button) {
            kywc_log(KYWC_DEBUG, "scroll_set_button(%d)", state->scroll_button);
            sucess &=
                log_status(libinput_device_config_scroll_set_button(device, state->scroll_button));
        }
        if (current->scroll_button_lock != state->scroll_button_lock) {
            kywc_log(KYWC_DEBUG, "scroll_set_button_lock(%d)", state->scroll_button_lock);
            sucess &= log_status(
                libinput_device_config_scroll_set_button_lock(device, state->scroll_button_lock));
        }
    }

    if (input->prop.has_dwt && current->dwt != state->dwt) {
        kywc_log(KYWC_DEBUG, "dwt_set_enabled(%d)", state->dwt);
        sucess &= log_status(libinput_device_config_dwt_set_enabled(device, state->dwt));
    }

    if (input->prop.has_dwtp && current->dwtp != state->dwtp) {
        kywc_log(KYWC_DEBUG, "dwtp_set_enabled(%d)", state->dwtp);
        sucess &= log_status(libinput_device_config_dwtp_set_enabled(device, state->dwtp));
    }

    if (input->prop.has_rotation && current->rotation_angle != state->rotation_angle) {
        kywc_log(KYWC_DEBUG, "rotation_set_angle(%d)", state->rotation_angle);
        sucess &=
            log_status(libinput_device_config_rotation_set_angle(device, state->rotation_angle));
    }

    return sucess;
}
