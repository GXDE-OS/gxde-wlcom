// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <linux/input-event-codes.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>

#include <kywc/binding.h>
#include <kywc/log.h>

#include "config.h"
#include "input/cursor.h"
#include "input/seat.h"
#include "server.h"
#include "util/dbus.h"
#include "util/spawn.h"
#include "util/string.h"

enum action_type {
    ACTION_TYPE_NONE = 0,
    ACTION_TYPE_DBUS_ACTION,
    ACTION_TYPE_RUN_COMMAND,
    ACTION_TYPE_SEND_BUTTON,
    ACTION_TYPE_SEND_KEY,
};

enum key_action {
    KEY_ACTION_PRESS = 1 << 0,
    KEY_ACTION_RELEASE = 1 << 1,
    KEY_ACTION_CLICK = (1 << 2) - 1,
};

enum input_type { INPUT_TYPE_NONE = 0, INPUT_TYPE_KEYBOARD, INPUT_TYPE_GESTURE };

enum dbus_type { DBUS_TYPE_NONE = 0, DBUS_TYPE_SESSION, DBUS_TYPE_SYSTEM };

enum control_type { CONTROL_TYPE_DELETE, CONTROL_TYPE_ENABLE, CONTROL_TYPE_DISABLE };

struct action_dbus_data {
    enum dbus_type type;
    char *service;
    char *path;
    char *interface;
    char *method;
};

struct action_command_data {
    char *cmd;
};

struct action_button_data {
    uint32_t val;
};

struct keycodes {
    enum key_action action;
    uint32_t *code;
    uint32_t len;
};

struct action_key_data {
    struct keycodes *modifiers;
    struct keycodes *keys;
};

struct action_data {
    enum action_type type;
    enum key_binding_type binding_type;
    bool enable;
    union {
        struct action_dbus_data dbus;
        struct action_button_data button;
        struct action_key_data key;
        struct action_command_data command;
    } data;
    const char *desc;
};

struct input_action {
    enum input_type type;
    char *bindings;
    struct action_data *action;

    void *data; /* key or gesture binding */

    struct wl_list link;
};

static struct input_action_manager {
    struct config *config;
    struct server *server;

    struct wl_listener server_destroy;

    struct wl_list actions;
} *manager = NULL;

static struct keycode_map {
    const char *key;
    uint32_t code;
} keycode_maps[] = {
    { "a", KEY_A },
    { "b", KEY_B },
    { "c", KEY_C },
    { "d", KEY_D },
    { "e", KEY_E },
    { "f", KEY_F },
    { "g", KEY_G },
    { "h", KEY_H },
    { "i", KEY_I },
    { "g", KEY_G },
    { "k", KEY_K },
    { "l", KEY_L },
    { "m", KEY_M },
    { "n", KEY_N },
    { "o", KEY_O },
    { "p", KEY_P },
    { "q", KEY_Q },
    { "r", KEY_R },
    { "s", KEY_S },
    { "t", KEY_T },
    { "u", KEY_U },
    { "v", KEY_V },
    { "w", KEY_W },
    { "x", KEY_X },
    { "y", KEY_Y },
    { "z", KEY_Z },
    { "Tab", KEY_TAB },
    { "Super_L", KEY_LEFTMETA },
    { "Alt_L", KEY_LEFTALT },
    { "Left", KEY_LEFT },
    { "Right", KEY_RIGHT },
    { "Down", KEY_DOWN },
    { "Up", KEY_UP },
    { "Shift_L", KEY_LEFTSHIFT },
    { "Control_R", KEY_RIGHTCTRL },
    { "Control_L", KEY_LEFTCTRL },
    { "Alt_R", KEY_RIGHTALT },
    { "Super_R", KEY_RIGHTMETA },
    { "Shift_R", KEY_RIGHTSHIFT },
};

static struct btncode_map {
    const char *btn;
    uint32_t code;
} btncode_maps[] = {
    { "left", BTN_LEFT }, { "right", BTN_RIGHT },     { "middle", BTN_MIDDLE },
    { "back", BTN_BACK }, { "forward", BTN_FORWARD },
};

static const char *service_path = "/com/kylin/Wlcom/InputAction";
static const char *service_interface = "com.kylin.Wlcom.InputAction";

static uint32_t keycode_map(const char *keystr)
{
    for (size_t i = 0; i < sizeof(keycode_maps) / sizeof(struct keycode_map); i++) {
        if (strcasecmp(keystr, keycode_maps[i].key) == 0) {
            return keycode_maps[i].code;
        }
    }
    return 0;
}

static const char *keycode_to_str(uint32_t code)
{
    for (size_t i = 0; i < sizeof(keycode_maps) / sizeof(struct keycode_map); i++) {
        if (keycode_maps[i].code == code) {
            return keycode_maps[i].key;
        }
    }
    return NULL;
}

static char *keycodes_to_str(struct keycodes *keycodes)
{
    char *str = NULL;
    for (uint32_t i = 0; i < keycodes->len; ++i) {
        const char *keystr = keycode_to_str(keycodes->code[i]);
        if (!keystr) {
            continue;
        }
        if (!str) {
            str = calloc(1, strlen(keystr) + 1);
            strcpy(str, keystr);
        } else {
            int new_size = strlen(str) + strlen(keystr) + 2;
            char *new_str = calloc(new_size, sizeof(char));
            snprintf(new_str, new_size, "%s+%s", str, keystr);
            free(str);
            str = new_str;
        }
    }
    return str;
}

static uint32_t btncode_map(const char *btnstr)
{
    for (size_t i = 0; i < sizeof(btncode_maps) / sizeof(struct btncode_map); i++) {
        if (strcasecmp(btnstr, btncode_maps[i].btn) == 0) {
            return btncode_maps[i].code;
        }
    }
    return 0;
}

static const char *btncode_to_str(uint32_t code)
{
    for (size_t i = 0; i < sizeof(btncode_maps) / sizeof(struct btncode_map); i++) {
        if (code == btncode_maps[i].code) {
            return btncode_maps[i].btn;
        }
    }
    return NULL;
}

static struct keycodes *keycodes_create(const char *str)
{
    struct keycodes *keycodes = calloc(1, sizeof(*keycodes));
    if (!keycodes) {
        return NULL;
    }

    size_t action_len = 0;
    char **split_action = string_split(str, ":", &action_len);
    if (action_len > 2) {
        kywc_log(KYWC_ERROR, "split key action error");
    }
    size_t len = 0;
    char **split_str = string_split(split_action[0], "+", &len);
    for (size_t i = 0, j = 0; i < len; i++) {
        uint32_t keycode = keycode_map(split_str[i]);
        if (!keycode) {
            continue;
        }
        keycodes->code = realloc(keycodes->code, (keycodes->len + 1) * sizeof(uint32_t));
        keycodes->code[j++] = keycode;
        keycodes->len++;
    }
    string_free_split(split_str);
    keycodes->action = KEY_ACTION_CLICK;
    if (action_len == 2) {
        if (strcmp(split_action[1], "press") == 0) {
            keycodes->action = KEY_ACTION_PRESS;
        } else if (strcmp(split_action[1], "release") == 0) {
            keycodes->action = KEY_ACTION_RELEASE;
        }
    }
    string_free_split(split_action);

    return keycodes;
}

static int list_input_actions(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct input_action_manager *manager = userdata;

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(ss)"));

    struct input_action *action;
    wl_list_for_each(action, &manager->actions, link) {
        json_object *itype_obj = json_object_object_get(
            manager->config->json, action->type == INPUT_TYPE_KEYBOARD ? "keyboard" : "gesture");
        json_object *config = json_object_object_get(itype_obj, action->bindings);
        const char *cfg = json_object_to_json_string(config);
        sd_bus_message_append(reply, "(ss)", action->bindings, cfg);
    }
    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static struct action_data *action_data_create_from_bus_string(const char *bus_str)
{
    struct action_data *action_data = NULL;
    size_t len = 0;
    char **split_str = string_split(bus_str, ",", &len);
    if (len < 2) {
        goto err;
    }

    action_data = calloc(1, sizeof(*action_data));
    if (!action_data) {
        goto err;
    }

    if (strcmp(split_str[0], "dbus") == 0) {
        action_data->type = ACTION_TYPE_DBUS_ACTION;
    } else if (strcmp(split_str[0], "command") == 0) {
        action_data->type = ACTION_TYPE_RUN_COMMAND;
    } else if (strcmp(split_str[0], "button") == 0) {
        action_data->type = ACTION_TYPE_SEND_BUTTON;
    } else if (strcmp(split_str[0], "key") == 0) {
        action_data->type = ACTION_TYPE_SEND_KEY;
    } else {
        action_data->type = ACTION_TYPE_NONE;
    }

    kywc_log(KYWC_DEBUG, "action type: %s, len: %ld", split_str[0], len);
    if (action_data->type == ACTION_TYPE_DBUS_ACTION && len == 6) {
        if (strcmp(split_str[1], "session") == 0) {
            action_data->data.dbus.type = DBUS_TYPE_SESSION;
        } else if (strcmp(split_str[1], "system") == 0) {
            action_data->data.dbus.type = DBUS_TYPE_SYSTEM;
        } else {
            action_data->data.dbus.type = DBUS_TYPE_NONE;
        }

        action_data->data.dbus.service = strdup(split_str[2]);
        action_data->data.dbus.path = strdup(split_str[3]);
        action_data->data.dbus.interface = strdup(split_str[4]);
        action_data->data.dbus.method = strdup(split_str[5]);
    } else if (action_data->type == ACTION_TYPE_RUN_COMMAND && len == 2) {
        action_data->data.command.cmd = strdup(split_str[1]);
    } else if (action_data->type == ACTION_TYPE_SEND_BUTTON && len == 2) {
        action_data->data.button.val = btncode_map(split_str[1]);
    } else if (action_data->type == ACTION_TYPE_SEND_KEY && len == 3) {
        action_data->data.key.modifiers = keycodes_create(split_str[1]);
        action_data->data.key.keys = keycodes_create(split_str[2]);
    } else {
        free(action_data);
        action_data = NULL;
        goto err;
    }

    action_data->enable = true;

err:
    string_free_split(split_str);

    return action_data;
}

static void action_call_dbus_method(struct action_dbus_data *dbus_data)
{
    bool ret = false;

    if (dbus_data->type == DBUS_TYPE_SESSION) {
        ret = dbus_call_method(dbus_data->service, dbus_data->path, dbus_data->interface,
                               dbus_data->method, NULL, NULL);
    } else if (dbus_data->type == DBUS_TYPE_SYSTEM) {
        ret = dbus_call_system_method(dbus_data->service, dbus_data->path, dbus_data->interface,
                                      dbus_data->method, NULL, NULL);
    }

    if (!ret) {
        kywc_log(KYWC_ERROR, "dbus call failed: %s %s %s %s", dbus_data->service, dbus_data->path,
                 dbus_data->interface, dbus_data->method);
    }
}

static void action_call_send_button(struct action_button_data *data)
{
    struct seat *seat = input_manager_get_default_seat();
    struct cursor *cursor = seat->cursor;

    if (cursor->touch_simulation_pointer && cursor->last_click_button == BTN_LEFT &&
        data->val == BTN_RIGHT) {
        seat_feed_pointer_button(seat, BTN_LEFT, false);
        cursor->touch_simulation_pointer = false;
    }

    seat_feed_pointer_button(seat, data->val, true);
    seat_feed_pointer_button(seat, data->val, false);
}

static void action_call_send_key(struct action_key_data *data)
{
    struct seat *seat = input_manager_get_default_seat();

    for (uint32_t i = 0; data->modifiers && i < data->modifiers->len; ++i) {
        if (data->modifiers->action & KEY_ACTION_PRESS) {
            seat_feed_keyboard_key(seat, data->modifiers->code[i], true);
        }
    }
    for (uint32_t i = 0; data->keys && i < data->keys->len; ++i) {
        if (data->keys->action & KEY_ACTION_PRESS) {
            seat_feed_keyboard_key(seat, data->keys->code[i], true);
        }
    }
    for (uint32_t i = 0; data->modifiers && i < data->modifiers->len; ++i) {
        if (data->modifiers->action & KEY_ACTION_RELEASE) {
            seat_feed_keyboard_key(seat, data->modifiers->code[i], false);
        }
    }
    for (uint32_t i = 0; data->keys && i < data->keys->len; ++i) {
        if (data->keys->action & KEY_ACTION_RELEASE) {
            seat_feed_keyboard_key(seat, data->keys->code[i], false);
        }
    }
}

static void handle_input_action(struct input_action *input_action)
{
    if (!input_action) {
        return;
    }

    struct action_data *action_data = input_action->action;
    switch (action_data->type) {
    case ACTION_TYPE_DBUS_ACTION:
        action_call_dbus_method(&action_data->data.dbus);
        break;
    case ACTION_TYPE_RUN_COMMAND:
        spawn_invoke(action_data->data.command.cmd);
        break;
    case ACTION_TYPE_SEND_BUTTON:
        action_call_send_button(&action_data->data.button);
        break;
    case ACTION_TYPE_SEND_KEY:
        action_call_send_key(&action_data->data.key);
        break;
    default:
        break;
    }

    kywc_log(KYWC_DEBUG, "input_action: %s, type: %d", input_action->bindings, action_data->type);
}

static void input_manager_keybinding_action(struct key_binding *binding, void *data)
{
    handle_input_action(data);
}

static void input_manager_gesturebinding_action(struct gesture_binding *binding, void *data,
                                                double dx, double dy)
{
    handle_input_action(data);
}

static void input_action_destroy(struct input_action *input_action)
{
    if (input_action->action->type == ACTION_TYPE_DBUS_ACTION) {
        free(input_action->action->data.dbus.service);
        free(input_action->action->data.dbus.path);
        free(input_action->action->data.dbus.interface);
        free(input_action->action->data.dbus.method);
    } else if (input_action->action->type == ACTION_TYPE_RUN_COMMAND) {
        free(input_action->action->data.command.cmd);
    } else if (input_action->action->type == ACTION_TYPE_SEND_KEY) {
        struct keycodes *modifiers = input_action->action->data.key.modifiers;
        struct keycodes *keys = input_action->action->data.key.keys;
        if (modifiers) {
            free(modifiers->code);
        }
        if (keys) {
            free(keys->code);
        }
        free(modifiers);
        free(keys);
    }

    wl_list_remove(&input_action->link);
    free(input_action->bindings);
    free(input_action->action);
    free(input_action);
}

static bool input_action_manager_delete_config(struct input_action_manager *manager,
                                               struct input_action *input_action)
{
    if (!manager->config || !manager->config->json || !input_action) {
        return false;
    }

    char *input_type = input_action->type == INPUT_TYPE_KEYBOARD ? "keyboard" : "gesture";
    json_object *config = json_object_object_get(manager->config->json, input_type);
    if (!config) {
        return false;
    }

    json_object_object_del(config, input_action->bindings);

    return true;
}

static bool input_action_manager_write_config(struct input_action_manager *manager,
                                              struct input_action *input_action)
{
    if (!manager->config || !manager->config->json || !input_action) {
        return false;
    }

    char *input_type = input_action->type == INPUT_TYPE_KEYBOARD ? "keyboard" : "gesture";
    json_object *config = json_object_object_get(manager->config->json, input_type);
    if (!config) {
        config = json_object_new_object();
        json_object_object_add(manager->config->json, input_type, config);
    }

    json_object *action_config = json_object_object_get(config, input_action->bindings);
    if (!action_config) {
        action_config = json_object_new_object();
        json_object_object_add(config, input_action->bindings, action_config);
    }

    struct action_data *action_data = input_action->action;
    json_object_object_add(action_config, "enable", json_object_new_boolean(action_data->enable));

    switch (action_data->type) {
    case ACTION_TYPE_DBUS_ACTION:
        json_object_object_add(action_config, "actiontype", json_object_new_string("dbus"));
        const char *bustype =
            action_data->data.dbus.type == DBUS_TYPE_SESSION ? "session" : "system";
        json_object_object_add(action_config, "bustype", json_object_new_string(bustype));
        json_object_object_add(action_config, "service",
                               json_object_new_string(action_data->data.dbus.service));
        json_object_object_add(action_config, "path",
                               json_object_new_string(action_data->data.dbus.path));
        json_object_object_add(action_config, "interface",
                               json_object_new_string(action_data->data.dbus.interface));
        json_object_object_add(action_config, "method",
                               json_object_new_string(action_data->data.dbus.method));
        break;
    case ACTION_TYPE_RUN_COMMAND:
        json_object_object_add(action_config, "actiontype", json_object_new_string("command"));
        json_object_object_add(action_config, "command",
                               json_object_new_string(action_data->data.command.cmd));
        break;
    case ACTION_TYPE_SEND_BUTTON:
        json_object_object_add(action_config, "actiontype", json_object_new_string("button"));

        const char *btnstr = btncode_to_str(action_data->data.button.val);
        if (btnstr) {
            json_object_object_add(action_config, "button", json_object_new_string(btnstr));
        }
        break;
    case ACTION_TYPE_SEND_KEY:
        json_object_object_add(action_config, "actiontype", json_object_new_string("key"));

        char *keystr = keycodes_to_str(action_data->data.key.modifiers);
        if (keystr) {
            json_object_object_add(action_config, "modifiers", json_object_new_string(keystr));
            free(keystr);
        }
        keystr = keycodes_to_str(action_data->data.key.keys);
        if (keystr) {
            json_object_object_add(action_config, "keys", json_object_new_string(keystr));
            free(keystr);
        }
        break;
    default:
        break;
    }

    if (action_data->desc) {
        json_object_object_add(action_config, "desc", json_object_new_string(action_data->desc));
    }

    return true;
}

static struct action_data *action_data_create_from_config(json_object *action_config)
{
    struct action_data *action_data = calloc(1, sizeof(*action_data));
    if (!action_data) {
        return NULL;
    }

    json_object *data;

    if (json_object_object_get_ex(action_config, "desc", &data)) {
        action_data->desc = json_object_get_string(data);
    }

    enum action_type type = ACTION_TYPE_NONE;
    if (json_object_object_get_ex(action_config, "actiontype", &data)) {
        const char *type_str = json_object_get_string(data);
        if (strcmp(type_str, "dbus") == 0) {
            type = ACTION_TYPE_DBUS_ACTION;
        } else if (strcmp(type_str, "command") == 0) {
            type = ACTION_TYPE_RUN_COMMAND;
        } else if (strcmp(type_str, "button") == 0) {
            type = ACTION_TYPE_SEND_BUTTON;
        } else if (strcmp(type_str, "key") == 0) {
            type = ACTION_TYPE_SEND_KEY;
        }
        action_data->type = type;
    }

    if (json_object_object_get_ex(action_config, "type", &data)) {
        const char *type_str = json_object_get_string(data);
        action_data->binding_type = kywc_key_binding_type_by_name(type_str, NULL);
    }

    switch (type) {
    case ACTION_TYPE_DBUS_ACTION:
        if (json_object_object_get_ex(action_config, "bustype", &data)) {
            const char *bustype = json_object_get_string(data);
            if (strcmp(bustype, "session") == 0) {
                action_data->data.dbus.type = DBUS_TYPE_SESSION;
            } else {
                action_data->data.dbus.type = DBUS_TYPE_SYSTEM;
            }
        }
        if (json_object_object_get_ex(action_config, "service", &data)) {
            action_data->data.dbus.service = strdup(json_object_get_string(data));
        }
        if (json_object_object_get_ex(action_config, "path", &data)) {
            action_data->data.dbus.path = strdup(json_object_get_string(data));
        }
        if (json_object_object_get_ex(action_config, "interface", &data)) {
            action_data->data.dbus.interface = strdup(json_object_get_string(data));
        }
        if (json_object_object_get_ex(action_config, "method", &data)) {
            action_data->data.dbus.method = strdup(json_object_get_string(data));
        }
        break;
    case ACTION_TYPE_RUN_COMMAND:
        if (json_object_object_get_ex(action_config, "command", &data)) {
            action_data->data.command.cmd = strdup(json_object_get_string(data));
        }
        break;
    case ACTION_TYPE_SEND_BUTTON:
        if (json_object_object_get_ex(action_config, "button", &data)) {
            const char *button = json_object_get_string(data);
            action_data->data.button.val = btncode_map(button);
        }
        break;
    case ACTION_TYPE_SEND_KEY:
        if (json_object_object_get_ex(action_config, "modifiers", &data)) {
            const char *modifiers = json_object_get_string(data);
            action_data->data.key.modifiers = keycodes_create(modifiers);
        }
        if (json_object_object_get_ex(action_config, "keys", &data)) {
            const char *keys = json_object_get_string(data);
            action_data->data.key.keys = keycodes_create(keys);
        }
        break;
    default:
        break;
    }

    if (json_object_object_get_ex(action_config, "enable", &data)) {
        action_data->enable = json_object_get_boolean(data);
    }

    return action_data;
}

static int add_input_action(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct input_action_manager *manager = userdata;

    const char *input_type = NULL, *input_bindings = NULL;
    const char *action_desc = NULL, *action_dat = NULL;
    const char *binding_type = NULL;
    CK(sd_bus_message_read(m, "sssss", &input_type, &input_bindings, &action_desc, &action_dat,
                           &binding_type));

    enum input_type itype;
    if (strcmp(input_type, "keyboard") == 0) {
        itype = INPUT_TYPE_KEYBOARD;
    } else if (strcmp(input_type, "gesture") == 0) {
        itype = INPUT_TYPE_GESTURE;
    } else {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid input_type.");
        return sd_bus_reply_method_error(m, &error);
    }

    void *binding =
        itype == INPUT_TYPE_KEYBOARD
            ? (void *)kywc_key_binding_create(input_bindings, action_desc)
            : (void *)kywc_gesture_binding_create_by_string(input_bindings, action_desc);
    if (!binding) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid input_bindings.");
        return sd_bus_reply_method_error(m, &error);
    }

    struct action_data *action_data = action_data_create_from_bus_string(action_dat);
    if (!action_data) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid action_data.");
        return sd_bus_reply_method_error(m, &error);
    }

    struct input_action *input_action = calloc(1, sizeof(*input_action));
    input_action->type = itype;
    input_action->bindings = strdup(input_bindings);
    input_action->action = action_data;
    action_data->desc = strdup(action_desc);

    if (itype == INPUT_TYPE_KEYBOARD) {
        action_data->binding_type = kywc_key_binding_type_by_name(binding_type, NULL);
    }

    if (action_data->enable) {
        bool ret = itype == INPUT_TYPE_KEYBOARD
                       ? kywc_key_binding_register(binding, action_data->binding_type,
                                                   input_manager_keybinding_action, input_action)
                       : kywc_gesture_binding_register(binding, input_manager_gesturebinding_action,
                                                       input_action);
        if (!ret) {
            itype == INPUT_TYPE_KEYBOARD ? kywc_key_binding_destroy(binding)
                                         : kywc_gesture_binding_destroy(binding);

            wl_list_init(&input_action->link);
            input_action_destroy(input_action);
            const sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS,
                                                               "Failed register input_bindings.");
            return sd_bus_reply_method_error(m, &error);
        }
    }

    input_action->data = binding;

    struct input_action *old, *temp;
    wl_list_for_each_safe(old, temp, &manager->actions, link) {
        if (strcmp(old->bindings, input_action->bindings) == 0) {
            input_action_destroy(old);
        }
    }

    input_action_manager_write_config(manager, input_action);

    wl_list_insert(&manager->actions, &input_action->link);

    return sd_bus_reply_method_return(m, NULL);
}

static int control_input_action(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct input_action_manager *manager = userdata;

    const char *control_type = NULL, *input_bindings = NULL;
    CK(sd_bus_message_read(m, "ss", &control_type, &input_bindings));
    enum control_type ctype;
    if (strcmp(control_type, "delete") == 0) {
        ctype = CONTROL_TYPE_DELETE;
    } else if (strcmp(control_type, "disable") == 0) {
        ctype = CONTROL_TYPE_DISABLE;
    } else if (strcmp(control_type, "enable") == 0) {
        ctype = CONTROL_TYPE_ENABLE;
    } else {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid control_type.");
        return sd_bus_reply_method_error(m, &error);
    }

    bool found = false;
    struct input_action *input_action = NULL;
    wl_list_for_each(input_action, &manager->actions, link) {
        if (strcmp(input_bindings, input_action->bindings)) {
            continue;
        }
        found = true;
        break;
    }

    if (!found) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid input_bindings.");
        return sd_bus_reply_method_error(m, &error);
    }

    switch (ctype) {
    case CONTROL_TYPE_DELETE:
        if (input_action->type == INPUT_TYPE_KEYBOARD && input_action->data) {
            kywc_key_binding_destroy(input_action->data);
        } else if (input_action->type == INPUT_TYPE_GESTURE && input_action->data) {
            kywc_gesture_binding_destroy(input_action->data);
        }

        input_action_manager_delete_config(manager, input_action);
        input_action_destroy(input_action);
        break;
    case CONTROL_TYPE_DISABLE:
        if (!input_action->action->enable) {
            break;
        }
        if (input_action->type == INPUT_TYPE_KEYBOARD && input_action->data) {
            kywc_key_binding_destroy(input_action->data);
        } else if (input_action->type == INPUT_TYPE_GESTURE && input_action->data) {
            kywc_gesture_binding_destroy(input_action->data);
        }

        input_action->data = NULL;
        input_action->action->enable = false;
        input_action_manager_write_config(manager, input_action);
        break;
    case CONTROL_TYPE_ENABLE:
        if (input_action->action->enable) {
            break;
        }
        if (input_action->type == INPUT_TYPE_KEYBOARD) {
            if (!input_action->data) {
                input_action->data =
                    kywc_key_binding_create(input_action->bindings, input_action->action->desc);
            }
            if (input_action->data) {
                if (!kywc_key_binding_register(input_action->data,
                                               input_action->action->binding_type,
                                               input_manager_keybinding_action, input_action)) {
                    kywc_key_binding_destroy(input_action->data);
                    input_action->data = NULL;
                }
            }
        } else if (input_action->type == INPUT_TYPE_GESTURE) {
            if (!input_action->data) {
                input_action->data = kywc_gesture_binding_create_by_string(
                    input_action->bindings, input_action->action->desc);
            }
            if (input_action->data) {
                if (!kywc_gesture_binding_register(
                        input_action->data, input_manager_gesturebinding_action, input_action)) {
                    kywc_key_binding_destroy(input_action->data);
                    input_action->data = NULL;
                }
            }
        }

        input_action->action->enable = true;
        input_action_manager_write_config(manager, input_action);
        break;
    default:
        break;
    }

    return sd_bus_reply_method_return(m, NULL);
}

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListAllActions", "", "a(ss)", list_input_actions, 0),
    SD_BUS_METHOD("AddAction", "sssss", "", add_input_action, 0),
    SD_BUS_METHOD("ControlAction", "ss", "", control_input_action, 0),
    SD_BUS_VTABLE_END,
};

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct input_action_manager *manager = wl_container_of(listener, manager, server_destroy);

    struct input_action *action, *temp;
    wl_list_for_each_safe(action, temp, &manager->actions, link) {
        input_action_destroy(action);
    }

    wl_list_remove(&manager->server_destroy.link);
    wl_list_remove(&manager->actions);
    free(manager);
}

static void input_action_create_with_keyboard(struct input_action_manager *manager,
                                              json_object *keyboard_obj, bool user)
{
    json_object_object_foreach(keyboard_obj, keybind, action_config) {
        struct action_data *action_data = action_data_create_from_config(action_config);
        if (!action_data) {
            continue;
        }

        struct input_action *input_action = calloc(1, sizeof(*input_action));
        input_action->bindings = strdup(keybind);
        input_action->type = INPUT_TYPE_KEYBOARD;
        input_action->action = action_data;

        kywc_log(KYWC_DEBUG, "input_action keybind: %s", keybind);

        wl_list_insert(&manager->actions, &input_action->link);

        if (!action_data->enable) {
            continue;
        }

        struct key_binding *binding = kywc_key_binding_create(keybind, action_data->desc);
        if (!binding) {
            continue;
        }

        if (!kywc_key_binding_register(binding, action_data->binding_type,
                                       input_manager_keybinding_action, input_action)) {
            kywc_log(KYWC_DEBUG, "key_binding registion failed");
            if (user) {
                json_object_object_del(keyboard_obj, keybind);
            }
            kywc_key_binding_destroy(binding);
            binding = NULL;
        }
        input_action->data = binding;
    }
}

static void input_action_create_with_gesture(struct input_action_manager *manager,
                                             json_object *gesture_obj, bool user)
{
    json_object_object_foreach(gesture_obj, gesture, action_config) {
        struct action_data *action_data = action_data_create_from_config(action_config);
        if (!action_data) {
            continue;
        }

        struct input_action *input_action = calloc(1, sizeof(*input_action));
        input_action->bindings = strdup(gesture);
        input_action->type = INPUT_TYPE_GESTURE;
        input_action->action = action_data;

        kywc_log(KYWC_DEBUG, "input_action gesture bind: %s", gesture);

        wl_list_insert(&manager->actions, &input_action->link);

        if (!action_data->enable) {
            continue;
        }

        struct gesture_binding *binding =
            kywc_gesture_binding_create_by_string(gesture, action_data->desc);
        if (!binding) {
            continue;
        }

        if (!kywc_gesture_binding_register(binding, input_manager_gesturebinding_action,
                                           input_action)) {
            kywc_log(KYWC_DEBUG, "gesture_binding registion failed");
            if (user) {
                json_object_object_del(gesture_obj, gesture);
            }
            kywc_gesture_binding_destroy(binding);
            binding = NULL;
        }
        input_action->data = binding;
    }
}

static bool input_action_manager_read_config(struct input_action_manager *manager)
{
    if (!manager->config || !manager->config->json) {
        return false;
    }

    json_object *data;
    /* get system default config */
    if (manager->config->sys_json &&
        json_object_object_get_ex(manager->config->sys_json, "keyboard", &data)) {
        input_action_create_with_keyboard(manager, data, false);
    }

    if (manager->config->sys_json &&
        json_object_object_get_ex(manager->config->sys_json, "gesture", &data)) {
        input_action_create_with_gesture(manager, data, false);
    }

    /* get user config */
    if (manager->config->json &&
        json_object_object_get_ex(manager->config->json, "keyboard", &data)) {
        input_action_create_with_keyboard(manager, data, true);
    }

    if (json_object_object_get_ex(manager->config->json, "gesture", &data)) {
        input_action_create_with_gesture(manager, data, true);
    }
    return true;
}

static bool input_action_manager_config_init(struct input_action_manager *manager)
{
    manager->config = config_manager_add_config("InputAction");
    if (!manager->config) {
        return false;
    }
    return dbus_register_object(NULL, service_path, service_interface, service_vtable, manager);
}

bool input_action_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(struct input_action_manager));
    if (!manager) {
        return false;
    }

    manager->server = server;
    wl_list_init(&manager->actions);

    input_action_manager_config_init(manager);
    input_action_manager_read_config(manager);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);

    return true;
}
