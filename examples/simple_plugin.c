// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdio.h>

#include <kywc/binding.h>
#include <kywc/plugin.h>

static struct kywc_plugin_info simple_plugin_info = {
    .name = "simple",
    .vendor = PLUGINVENDORSTRING,
    .class = "example",
    .description = "A simple plugin example",
    .version = PLUGIN_VERSION_NUMERIC(1, 0, 0),
    .abi_version = ABI_VERSION(1, 0),
};

static struct kywc_plugin_option simple_plugin_options[] = {
    { "option1", { .boolean = true }, option_type_boolean, 0 },
    { "option2", { .realnum = 1.0f }, option_type_double, 1 },
    { "option3", { .num = 1 }, option_type_int, 2 },
    { "option4", { .str = "one" }, option_type_string, 3 },
    { NULL, { 0 }, option_type_null, -1 },
};

static struct shortcut {
    char *keybind;
    char *desc;
    struct key_binding *binding;
} shortcuts[] = {
    { "Alt+t", "Alt+t trigger", NULL },
    { "Ctrl+t", "Ctrl+t trigger", NULL },
    { "Win+t", "Win+t trigger", NULL },
    { "Alt+Shift+t", "Alt+Shift+t trigger", NULL },
    { "Ctrl+Shift+t", "Ctrl+Shift+t trigger", NULL },
    { "Win+Shift+t", "Win+Shift+t rigger", NULL },
};

static struct gesture {
    char *gesture_str;
    char *desc;
    struct gesture_binding *binding;
} gestures[] = {
    { "pinch:any:2:inward+outward+clockwise", "2 fingers pinch in out or clockwise trigger", NULL },
    { "swipe:touch:1:left:right", "1 fingers swipe from right edge to left trigger", NULL },
    { "swipe:touch:2:left+right:left+right", "2 fingers swipe from left/right edge trigger", NULL },
    { "swipe:touch:3:up:bottom", "3 fingers swipe from top edge down trigger", NULL },
    { "swipe:any:4:up+down:none", "4 fingers swipe up down trigger", NULL },
    { "swipe:touchpad:4:left", "4 fingers touch pad swipe trigger", NULL },
    { "swipe:touch:5:left", "5 fingers touch swipe trigger", NULL },
};

static void shortcut_action(struct key_binding *binding, void *data)
{
    struct shortcut *shortcut = data;

    printf("simple plugin call action: %s\n", shortcut->desc);
}

static void gesture_action(struct gesture_binding *binding, void *data)
{
    struct gesture *gesture = data;

    printf("simple plugin call action: %s\n", gesture->desc);
}

static void simple_plugin_register_shortcuts(void)
{
    for (size_t i = 0; i < sizeof(shortcuts) / sizeof(struct shortcut); i++) {
        struct shortcut *shortcut = &shortcuts[i];
        struct key_binding *binding = kywc_key_binding_create(shortcut->keybind, shortcut->desc);
        if (!binding) {
            continue;
        }

        if (!kywc_key_binding_register(binding, shortcut_action, shortcut)) {
            kywc_key_binding_destroy(binding);
            continue;
        }

        shortcut->binding = binding;
    }

    // gesture
    for (size_t i = 0; i < sizeof(gestures) / sizeof(struct gesture); i++) {
        struct gesture *gesture = &gestures[i];
        struct gesture_binding *binding =
            kywc_gesture_binding_create_by_string(gesture->gesture_str, gesture->desc);
        if (!binding) {
            continue;
        }

        if (!kywc_gesture_binding_register(binding, gesture_action, gesture)) {
            kywc_gesture_binding_destroy(binding);
            continue;
        }
        gesture->binding = binding;
    }
}

static void simple_plugin_unregister_shortcuts(void)
{
    for (size_t i = 0; i < sizeof(shortcuts) / sizeof(struct shortcut); i++) {
        struct shortcut *shortcut = &shortcuts[i];
        if (shortcut->binding) {
            kywc_key_binding_destroy(shortcut->binding);
        }
    }

    // gesture
    for (size_t i = 0; i < sizeof(gestures) / sizeof(struct gesture); i++) {
        struct gesture *gesture = &gestures[i];
        if (gesture->binding) {
            kywc_gesture_binding_destroy(gesture->binding);
        }
    }
}

static bool simple_plugin_setup(void *plugin, void **teardown_data)
{
    printf("simple plugin setup called\n");

    option_value value = { 0 };

    if (kywc_plugin_get_option_boolean(plugin, 0, &value.boolean)) {
        printf("plugin option1 value is %s\n", value.boolean ? "true" : "false");
    }
    if (kywc_plugin_get_option_double(plugin, 1, &value.realnum)) {
        printf("plugin option2 value is %f\n", value.realnum);
    }
    if (kywc_plugin_get_option_int(plugin, 2, &value.num)) {
        printf("plugin option3 value is %d\n", value.num);
    }
    value.str = kywc_plugin_get_option_string(plugin, 3);
    printf("plugin option4 value is %s\n", value.str);

    simple_plugin_register_shortcuts();

    return true;
}

static void simple_plugin_option(struct kywc_plugin_option *option)
{
    printf("simple plugin option: %s", option->name);
    if (option->type == option_type_boolean) {
        printf(" value: %s\n", option->value.boolean ? "true" : "false");
    } else if (option->type == option_type_double) {
        printf(" value: %f\n", option->value.realnum);
    } else if (option->type == option_type_int) {
        printf(" value: %d\n", option->value.num);
    } else if (option->type == option_type_string) {
        printf(" value: %s\n", option->value.str);
    }
}

static void simple_plugin_teardown(void *teardown_data)
{
    printf("simple plugin teardown called\n");

    simple_plugin_unregister_shortcuts();
}

struct kywc_plugin_data simple_plugin_data = {
    .info = &simple_plugin_info,
    .options = simple_plugin_options,
    .setup = simple_plugin_setup,
    .option = simple_plugin_option,
    .teardown = simple_plugin_teardown,
};
