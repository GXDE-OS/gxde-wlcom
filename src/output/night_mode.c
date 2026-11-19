// SPDX-FileCopyrightText: 2026 CharOfString <root@charofstring.cc>
//
// SPDX-License-Identifier: GPL-3.0-or-later

/* Originally implemented in FlakeWM, ported here. */

#include <math.h>
#include <stdlib.h>

#include "config.h"
#include "output_p.h"
#include "util/dbus.h"
#include "util/time.h"

#define NIGHT_MODE_TEMP_MIN 1000
#define NIGHT_MODE_TEMP_MAX 6500
#define NIGHT_MODE_TEMP_DEFAULT 4500

static const char *night_mode_service = "top.gxde.Wlcom.NightMode";
static const char *night_mode_path = "/top/gxde/Wlcom/NightMode";
static const char *night_mode_interface = "top.gxde.Wlcom.NightMode";

struct night_mode {
    struct config *config;
    struct wl_listener config_destroy;
    struct wl_listener server_destroy;

    bool enabled;
    uint32_t temperature;

    /**
     * What is on screen: the night temperature `temp_current` is blended over
     * each output's own color temperature by `blend` (0 off, 1 fully on), and
     * both ease towards enabled/temperature.
     */
    struct wl_event_source *timer;
    float blend, blend_from;
    float temp_current, temp_from;
    uint32_t anim_start;
};

static struct night_mode *night_mode = NULL;

static bool temperature_is_valid(int64_t temperature)
{
    return temperature >= NIGHT_MODE_TEMP_MIN && temperature <= NIGHT_MODE_TEMP_MAX;
}

uint32_t output_effective_color_temp(struct output *output)
{
    uint32_t own = output->base.state.color_temp;
    if (!night_mode || night_mode->blend <= 0.0f) {
        return own;
    }
    float temp = own + (night_mode->temp_current - (float)own) * night_mode->blend;
    return (uint32_t)lroundf(temp);
}

static bool update_output(struct kywc_output *kywc_output, int index, void *data)
{
    output_update_gamma(output_from_kywc_output(kywc_output));
    return false;
}

static float target_blend(void)
{
    return night_mode->enabled ? 1.0f : 0.0f;
}

static void snap_to_target(void)
{
    night_mode->blend = target_blend();
    night_mode->temp_current = night_mode->temperature;
}

static int handle_transition(void *data)
{
    float t = output_gamma_transition_progress(night_mode->anim_start);
    if (t >= 1.0f) {
        snap_to_target();
    } else {
        night_mode->blend =
            night_mode->blend_from + (target_blend() - night_mode->blend_from) * t;
        night_mode->temp_current =
            night_mode->temp_from + ((float)night_mode->temperature - night_mode->temp_from) * t;
        wl_event_source_timer_update(night_mode->timer, OUTPUT_GAMMA_TRANSITION_TICK_MS);
    }
    output_manager_for_each_output(update_output, true, NULL);
    return 0;
}

static void start_transition(void)
{
    if (night_mode->blend <= 0.0f) {
        /* fading in from off: start at the new temperature, only blend in */
        night_mode->temp_current = night_mode->temperature;
        if (!night_mode->enabled) {
            /* off and staying off, nothing changes on screen */
            return;
        }
    }

    if (!night_mode->timer) {
        snap_to_target();
        output_manager_for_each_output(update_output, true, NULL);
        return;
    }

    /* restart from wherever a running transition currently is */
    night_mode->blend_from = night_mode->blend;
    night_mode->temp_from = night_mode->temp_current;
    night_mode->anim_start = current_time_msec();
    wl_event_source_timer_update(night_mode->timer, 1);
}

static void night_mode_changed(void)
{
    start_transition();

    if (night_mode->config && night_mode->config->json) {
        json_object *json = night_mode->config->json;
        json_object_object_add(json, "enabled", json_object_new_boolean(night_mode->enabled));
        json_object_object_add(json, "temperature",
                               json_object_new_int64(night_mode->temperature));
        config_manager_sync();
    }

    kywc_log(KYWC_INFO, "night mode %s, temperature %uK", night_mode->enabled ? "on" : "off",
             night_mode->temperature);
    dbus_emit_signal(night_mode_path, night_mode_interface, "Changed", "bu",
                     (int)night_mode->enabled, night_mode->temperature);
}

static void read_config(void)
{
    if (!night_mode->config || !night_mode->config->json) {
        return;
    }

    json_object *data;
    if (json_object_object_get_ex(night_mode->config->json, "enabled", &data) &&
        json_object_is_type(data, json_type_boolean)) {
        night_mode->enabled = json_object_get_boolean(data);
    }
    if (json_object_object_get_ex(night_mode->config->json, "temperature", &data) &&
        json_object_is_type(data, json_type_int)) {
        int64_t temperature = json_object_get_int64(data);
        if (temperature_is_valid(temperature)) {
            night_mode->temperature = temperature;
        }
    }
}

static int reply_not_ready(sd_bus_message *m)
{
    sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_FAILED, "Night mode is not ready.");
    return sd_bus_reply_method_error(m, &error);
}

static int get_enabled(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    if (!night_mode) {
        return reply_not_ready(m);
    }
    return sd_bus_reply_method_return(m, "b", (int)night_mode->enabled);
}

static int set_enabled(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    int enabled = 0;
    CK(sd_bus_message_read(m, "b", &enabled));
    if (!night_mode) {
        return reply_not_ready(m);
    }

    if (night_mode->enabled != !!enabled) {
        night_mode->enabled = !!enabled;
        night_mode_changed();
    }
    return sd_bus_reply_method_return(m, "b", 1);
}

static int get_temperature(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    if (!night_mode) {
        return reply_not_ready(m);
    }
    return sd_bus_reply_method_return(m, "u", night_mode->temperature);
}

static int set_temperature(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    uint32_t temperature = 0;
    CK(sd_bus_message_read(m, "u", &temperature));
    if (!night_mode) {
        return reply_not_ready(m);
    }
    if (!temperature_is_valid(temperature)) {
        sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(
            SD_BUS_ERROR_INVALID_ARGS, "Temperature must be between 1000 and 6500 kelvin.");
        return sd_bus_reply_method_error(m, &error);
    }

    if (night_mode->temperature != temperature) {
        night_mode->temperature = temperature;
        night_mode_changed();
    }
    return sd_bus_reply_method_return(m, NULL);
}

static const sd_bus_vtable night_mode_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetEnabled", "", "b", get_enabled, 0),
    SD_BUS_METHOD("SetEnabled", "b", "b", set_enabled, 0),
    SD_BUS_METHOD("GetTemperature", "", "u", get_temperature, 0),
    SD_BUS_METHOD("SetTemperature", "u", "", set_temperature, 0),
    SD_BUS_SIGNAL("Changed", "bu", 0),
    SD_BUS_VTABLE_END,
};

static void handle_config_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&night_mode->config_destroy.link);
    wl_list_init(&night_mode->config_destroy.link);
    night_mode->config = NULL;
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    if (night_mode->timer) {
        wl_event_source_remove(night_mode->timer);
    }
    wl_list_remove(&night_mode->config_destroy.link);
    wl_list_remove(&night_mode->server_destroy.link);
    free(night_mode);
    night_mode = NULL;
}

bool output_night_mode_create(struct server *server)
{
    night_mode = calloc(1, sizeof(*night_mode));
    if (!night_mode) {
        return false;
    }

    night_mode->temperature = NIGHT_MODE_TEMP_DEFAULT;
    wl_list_init(&night_mode->config_destroy.link);

    night_mode->config = config_manager_add_config("NightMode");
    if (night_mode->config) {
        night_mode->config_destroy.notify = handle_config_destroy;
        wl_signal_add(&night_mode->config->events.destroy, &night_mode->config_destroy);
        read_config();
    }
    /* the saved state shows up at startup without a transition */
    snap_to_target();
    night_mode->timer = wl_event_loop_add_timer(server->event_loop, handle_transition, NULL);

    night_mode->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &night_mode->server_destroy);

    if (!dbus_register_object(night_mode_service, night_mode_path, night_mode_interface,
                              night_mode_vtable, NULL)) {
        kywc_log(KYWC_WARN, "%s register failed", night_mode_interface);
    }
    return true;
}
