// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _KYWC_PLUGIN_H_
#define _KYWC_PLUGIN_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* inspired by xorg module */

#define PLUGIN_VERSION_NUMERIC(major, minor, micro)                                                \
    ((((major) & 0xFF) << 24) | (((minor) & 0xFF) << 16) | ((micro) & 0xFFFF))
#define GET_PLUGIN_MAJOR_VERSION(vers) (((vers) >> 24) & 0xFF)
#define GET_PLUGIN_MINOR_VERSION(vers) (((vers) >> 16) & 0xFF)
#define GET_PLUGIN_MICRO_VERSION(vers) ((vers) & 0xFFFF)

#define ABI_MINOR_MASK 0x0000FFFF
#define ABI_MAJOR_MASK 0xFFFF0000
#define GET_ABI_MINOR(v) ((v) & ABI_MINOR_MASK)
#define GET_ABI_MAJOR(v) (((v) & ABI_MAJOR_MASK) >> 16)
#define ABI_VERSION(maj, min) ((((maj) << 16) & ABI_MAJOR_MASK) | ((min) & ABI_MINOR_MASK))

/* default plugin vender */
#ifndef PLUGINVENDORSTRING
#define PLUGINVENDORSTRING "KylinSoft Multimedia"
#endif

struct kywc_plugin_info {
    const char *name;        /* name of the plugin  */
    const char *vendor;      /* who write this plugin */
    const char *class;       /* plugin class */
    const char *description; /* plugin description */

    uint32_t version;     /* version of this plugin */
    uint32_t abi_version; /* abi version the plugin use */
};

typedef union {
    int32_t num;
    const char *str;
    double realnum;
    bool boolean;
} option_value;

typedef enum {
    option_type_null = 0,
    option_type_boolean,
    option_type_double,
    option_type_int,
    option_type_string,
} option_type;

struct kywc_plugin_option {
    const char *name;
    option_value value;
    option_type type;
    int32_t token;
};

static __attribute__((unused)) inline bool kywc_plugin_option_match(struct kywc_plugin_option *a,
                                                                    struct kywc_plugin_option *b)
{
    if (strcmp(a->name, b->name) || a->type != b->type) {
        return false;
    }

    return true;
}

static __attribute__((unused)) inline bool
kywc_plugin_option_value_equal(option_value a, option_value b, option_type type)

{
    if (type == option_type_string) {
        return !strcmp(a.str, b.str);
    } else if (type == option_type_double) {
        return a.realnum == b.realnum;
    } else if (type == option_type_int) {
        return a.num == b.num;
    } else if (type == option_type_boolean) {
        return a.boolean == b.boolean;
    }

    return false;
}

/**
 * get option value by token.
 * return false or NULL means not find this token.
 */
bool kywc_plugin_get_option_boolean(void *plugin, int32_t token, bool *value);
bool kywc_plugin_get_option_double(void *plugin, int32_t token, double *value);
bool kywc_plugin_get_option_int(void *plugin, int32_t token, int *value);
const char *kywc_plugin_get_option_string(void *plugin, int32_t token);

/**
 * called every time the plugin is enabled,
 * teardown_data will passed to teardown function to release resources.
 */
typedef bool (*plugin_setup_func)(void *plugin, void **teardown_data);

/**
 * called every time the plugin is disabled.
 */
typedef void (*plugin_teardown_func)(void *teardown_data);

/**
 *  apply one option in runtime.
 */
typedef void (*plugin_option_func)(struct kywc_plugin_option *option);

/**
 * plugins need export this data symbol: xxx_plugin_data
 * max options number is 64.
 */
struct kywc_plugin_data {
    struct kywc_plugin_info *info;
    struct kywc_plugin_option *options; /* read only, maybe null */
    plugin_setup_func setup;
    plugin_option_func option; /* maybe null */
    plugin_teardown_func teardown;
};

#endif /* _KYWC_PLUGIN_H_ */
