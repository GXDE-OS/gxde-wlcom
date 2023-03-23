#include <stdio.h>

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
}

struct kywc_plugin_data simple_plugin_data = {
    .info = &simple_plugin_info,
    .options = simple_plugin_options,
    .setup = simple_plugin_setup,
    .option = simple_plugin_option,
    .teardown = simple_plugin_teardown,
};
