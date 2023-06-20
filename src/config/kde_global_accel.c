#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// for enum wlr_keyboard_modifier
#include <wlr/types/wlr_keyboard.h>

#include <kywc/binding.h>
#include <kywc/log.h>

#include "config_p.h"
#include "qtkey_map_table.h"

/**
 * convert qtkey to xkb keysym and modifiers
 */

enum Key {
    Key_0 = 0x30,
    Key_9 = 0x39,

    NoModifier = 0x00000000,
    ShiftModifier = 0x02000000,
    ControlModifier = 0x04000000,
    AltModifier = 0x08000000,
    MetaModifier = 0x10000000,
    KeypadModifier = 0x20000000,
    GroupSwitchModifier = 0x40000000,
    // Do not extend the mask to include 0x01000000
    KeyboardModifierMask = 0xfe000000
};

static int compare_key(const void *p1, const void *p2)
{
    int32_t k1 = ((struct qtkey_map *)p1)->qtkey;
    int32_t k2 = ((struct qtkey_map *)p2)->qtkey;
    return k1 > k2 ? 1 : (k1 == k2 ? 0 : -1);
}

static uint32_t qtkey_to_modifiers(int32_t key)
{
    uint32_t modifiers = 0;

    if (key & ShiftModifier) {
        modifiers |= WLR_MODIFIER_SHIFT;
    }
    if (key & ControlModifier) {
        modifiers |= WLR_MODIFIER_CTRL;
    }
    if (key & AltModifier) {
        modifiers |= WLR_MODIFIER_ALT;
    }
    if (key & MetaModifier) {
        modifiers |= WLR_MODIFIER_LOGO;
    }
    if (key & KeypadModifier) {
        modifiers |= WLR_MODIFIER_MOD2;
    }
    if (key & GroupSwitchModifier) {
        modifiers |= WLR_MODIFIER_MOD5;
    }

    return modifiers;
}

static uint32_t qtkey_to_keysym(int32_t key)
{
    uint32_t sym = XKB_KEY_NoSymbol;
    uint32_t qtKey = key & ~KeyboardModifierMask;

    if (key & KeypadModifier && qtKey >= Key_0 && qtKey <= Key_9) {
        sym = XKB_KEY_KP_0 + qtKey - Key_0;
    } else if (qtKey < 0x1000 && !(key & KeypadModifier)) {
        if (!(key & ShiftModifier)) {
            qtKey = tolower(qtKey);
        }
        sym = qtKey;
    }

    /* bsearch the key_map_table */
    if (sym == XKB_KEY_NoSymbol) {
        struct qtkey_map k = { .qtkey = qtKey };
        struct qtkey_map *map =
            bsearch(&k, qtkey_map_table, KEY_MAP_COUNT, sizeof(struct qtkey_map), compare_key);
        if (map) {
            sym = map->keysym;
        }
    }

    if (sym == XKB_KEY_NoSymbol) {
        kywc_log(KYWC_WARN, "cannot covert qtkey %d to xkb symbol", qtKey);
    }

    return sym;
}

/**
 * dbus server for kde kglobalaccel
 */

/* Index for actionId QStringLists */
enum actionIdFields {
    ComponentUnique = 0,   //!< Components Unique Name (ID)
    ActionUnique = 1,      //!< Actions Unique Name(ID)
    ComponentFriendly = 2, //!< Components Friendly Translated Name
    ActionFriendly = 3,    //!< Actions Friendly Translated Name
};

enum SetShortcutFlag {
    SetPresent = 2,
    NoAutoloading = 4,
    IsDefault = 8,
};

struct global_shortcut_registry {
    struct config *config;
    struct wl_list components;

    struct wl_listener destroy;
};

struct global_shortcut_component {
    struct wl_list link;

    /* dbus for per component */
    struct config *config;
    const char *dbus_path;

    char *unique_name;
    char *friendly_name;

    struct wl_list contexts;
    /* current context in component, "default" "Default Context" */
    struct global_shortcut_context *current;
};

struct global_shortcut_context {
    struct wl_list link;
    struct global_shortcut_component *component;

    char *unique_name;
    char *friendly_name;

    struct wl_list shortcuts;
};

struct global_shortcut {
    struct wl_list link;
    struct global_shortcut_context *context;

    char *unique_name;
    char *friendly_name;

    struct key_binding *binding;

    /* keys. default_keys, mostly combined with modifiers */
    int32_t key;
    int32_t default_key;

    /* means the associated application is present */
    bool is_present;
    /* means the shortcut is registered with key_binding */
    bool is_registered;
    /* means the shortcut is new */
    bool is_fresh;
};

// TODO: add global_shortcut_action to support key list

static const char *registry_bus = "org.kde.kglobalaccel";
static const char *registry_path = "/kglobalaccel";
static const char *registry_interface = "org.kde.KGlobalAccel";
static const char *component_path_prefix = "/component";
static const char *component_interface = "org.kde.kglobalaccel.Component";

struct global_shortcut_registry *registry = NULL;

static struct global_shortcut_component *
global_shortcut_registry_get_component(const char *unique_name)
{
    struct global_shortcut_component *component;
    wl_list_for_each(component, &registry->components, link) {
        if (strcmp(unique_name, component->unique_name) == 0) {
            return component;
        }
    }

    return NULL;
}

static struct global_shortcut *global_shortcut_create(struct global_shortcut_context *context,
                                                      const char *unique_name,
                                                      const char *friendly_name)
{
    struct global_shortcut *shortcut = calloc(1, sizeof(struct global_shortcut));
    if (!shortcut) {
        return NULL;
    }

    shortcut->is_fresh = true;
    shortcut->is_present = false;
    shortcut->is_registered = false;

    shortcut->unique_name = strdup(unique_name);
    shortcut->friendly_name = strdup(friendly_name);

    shortcut->context = context;
    wl_list_insert(&context->shortcuts, &shortcut->link);

    return shortcut;
}

static void global_shortcut_destroy(struct global_shortcut *shortcut, bool clean)
{
    wl_list_remove(&shortcut->link);

    free(shortcut->unique_name);
    free(shortcut->friendly_name);

    /* destroyed in runtime */
    if (clean && shortcut->binding) {
        kywc_key_binding_destroy(shortcut->binding);
    }

    free(shortcut);
}

static struct global_shortcut_context *
global_shortcut_context_create(struct global_shortcut_component *component, const char *unique_name,
                               const char *friendly_name)
{
    struct global_shortcut_context *context = calloc(1, sizeof(struct global_shortcut_context));
    if (!context) {
        return NULL;
    }

    wl_list_init(&context->shortcuts);
    wl_list_insert(&component->contexts, &context->link);

    context->component = component;
    context->unique_name = strdup(unique_name);
    context->friendly_name = strdup(friendly_name);

    return context;
}

static void global_shortcut_context_destroy(struct global_shortcut_context *context)
{
    wl_list_remove(&context->link);

    free(context->unique_name);
    free(context->friendly_name);

    struct global_shortcut *shortcut, *tmp;
    wl_list_for_each_safe(shortcut, tmp, &context->shortcuts, link) {
        global_shortcut_destroy(shortcut, false);
    }

    free(context);
}

static struct global_shortcut_context *
global_shortcut_component_get_context(struct global_shortcut_component *component,
                                      const char *context_name)
{
    struct global_shortcut_context *context;
    wl_list_for_each(context, &component->contexts, link) {
        if (strcmp(context->unique_name, context_name) == 0) {
            return context;
        }
    }

    return NULL;
}

static struct global_shortcut *
global_shortcut_context_get_shortcut_by_key(struct global_shortcut_context *context, int32_t key)
{
    struct global_shortcut *shortcut = NULL;

    wl_list_for_each(shortcut, &context->shortcuts, link) {
        if (shortcut->key == key) {
            return shortcut;
        }
    }

    return NULL;
}

static struct global_shortcut *
global_shortcut_context_get_shortcut_by_name(struct global_shortcut_context *context,
                                             const char *shortcut_unique)
{
    struct global_shortcut *shortcut = NULL;

    wl_list_for_each(shortcut, &context->shortcuts, link) {
        if (strcmp(shortcut_unique, shortcut->unique_name) == 0) {
            return shortcut;
        }
    }

    return NULL;
}

/* get shortcut in component current context */
static struct global_shortcut *global_shortcut_registry_get_shortcut_by_key(int32_t key)
{
    struct global_shortcut *shortcut;

    struct global_shortcut_component *component;
    wl_list_for_each(component, &registry->components, link) {
        shortcut = global_shortcut_context_get_shortcut_by_key(component->current, key);
        if (shortcut) {
            return shortcut;
        }
    }

    return NULL;
}

static struct global_shortcut *
global_shortcut_registry_get_shortcut_by_name(const char *component_unique,
                                              const char *shortcut_unique)
{
    struct global_shortcut_component *component =
        global_shortcut_registry_get_component(component_unique);
    if (component) {
        struct global_shortcut *shortcut =
            global_shortcut_context_get_shortcut_by_name(component->current, shortcut_unique);
        if (shortcut) {
            return shortcut;
        }
    }

    return NULL;
}

static void global_shortcut_action(struct key_binding *bindbing, void *data)
{
    struct global_shortcut *shortcut = data;
    sd_bus_emit_signal(sd_bus_slot_get_bus(registry->config->slot),
                       shortcut->context->component->dbus_path, component_interface,
                       "globalShortcutPressed", "ssx", shortcut->context->component->unique_name,
                       shortcut->unique_name, 0);
}

static void global_shortcut_create_binding(struct global_shortcut *shortcut)
{
    uint32_t keysym = qtkey_to_keysym(shortcut->key);
    uint32_t modifiers = qtkey_to_modifiers(shortcut->key);

    if (shortcut->binding) {
        kywc_key_binding_update(shortcut->binding, keysym, modifiers, NULL);
    } else {
        shortcut->binding =
            kywc_key_binding_create_by_symbol(keysym, modifiers, shortcut->unique_name);
    }
}

static void global_shortcut_set_active(struct global_shortcut *shortcut)
{
    if (!shortcut->is_present || shortcut->is_registered) {
        return;
    }

    /* register the key binding */
    kywc_key_binding_register(shortcut->binding, global_shortcut_action, shortcut);
    shortcut->is_registered = true;
}

static void global_shortcut_set_inactive(struct global_shortcut *shortcut)
{
    if (!shortcut->is_registered) {
        return;
    }

    kywc_key_binding_unregister(shortcut->binding);
    shortcut->is_registered = false;
}

/**
 * dbus process fuctions for org.kde.kglobalaccel.Component
 */

// SD_BUS_PROPERTY("friendlyName", "s", friendly_name, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
static int friendly_name(sd_bus *bus, const char *path, const char *interface, const char *property,
                         sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct global_shortcut_component *componet = userdata;
    CK(sd_bus_message_append_basic(reply, 's', componet->friendly_name));
    return 0;
}

static int unique_name(sd_bus *bus, const char *path, const char *interface, const char *property,
                       sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct global_shortcut_component *componet = userdata;
    CK(sd_bus_message_append_basic(reply, 's', componet->unique_name));
    return 0;
}

// SD_BUS_METHOD("allShortcutInfos", "s", "a(ssssssaiai)", all_shortcut_infos_ex, 0),
static int all_shortcut_infos(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *context_unique = NULL;
    CK(sd_bus_message_read(msg, "s", &context_unique));

    struct global_shortcut_component *component = userdata;
    struct global_shortcut_context *context = global_shortcut_component_get_context(
        component, context_unique ? context_unique : "default");

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(msg, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(ssssssaiai)"));

    if (context) {
        struct global_shortcut *shortcut;
        wl_list_for_each(shortcut, &context->shortcuts, link) {
            CK(sd_bus_message_append(reply, "(ssssssaiai)", context->unique_name,
                                     context->friendly_name, component->unique_name,
                                     component->friendly_name, shortcut->unique_name,
                                     shortcut->friendly_name, 4, shortcut->key, 0, 0, 0, 4,
                                     shortcut->default_key, 0, 0, 0));
        }
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

// SD_BUS_METHOD("cleanUp", "", "b", clean_up, 0),
static int clean_up(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    struct global_shortcut_component *component = userdata;
    bool changed = false;

    struct global_shortcut *shortcut;
    wl_list_for_each(shortcut, &component->current->shortcuts, link) {
        if (!shortcut->is_present) {
            changed = true;
            global_shortcut_destroy(shortcut, true);
        }
    }

    return sd_bus_reply_method_return(msg, "b", changed);
}

// SD_BUS_METHOD("getShortcutContexts", "", "as", get_shortcut_contexts, 0),
static int get_shortcut_contexts(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    struct global_shortcut_component *component = userdata;

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(msg, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "s"));

    struct global_shortcut_context *context;
    wl_list_for_each(context, &component->contexts, link) {
        CK(sd_bus_message_append(reply, "s", context->unique_name));
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

// SD_BUS_METHOD("invokeShortcut", "ss", "", invoke_shortcut, 0),
static int invoke_shortcut(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *shortcut_unique, *context_unique = NULL;
    CK(sd_bus_message_read(msg, "ss", &shortcut_unique, &context_unique));

    struct global_shortcut_component *component = userdata;
    struct global_shortcut_context *context = global_shortcut_component_get_context(
        component, context_unique ? context_unique : "default");

    if (context) {
        struct global_shortcut *shortcut =
            global_shortcut_context_get_shortcut_by_name(context, shortcut_unique);
        if (shortcut) {
            sd_bus_emit_signal(sd_bus_slot_get_bus(registry->config->slot),
                               shortcut->context->component->dbus_path, component_interface,
                               "globalShortcutPressed", "ssx",
                               shortcut->context->component->unique_name, shortcut->unique_name, 0);
        }
    }

    return sd_bus_reply_method_return(msg, NULL);
}

// SD_BUS_METHOD("isActive", "", "b", is_active, 0),
static int is_active(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    struct global_shortcut_component *component = userdata;
    bool is_active = false;

    // The component is active if at least one of it's global shortcuts is present.
    struct global_shortcut *shortcut;
    wl_list_for_each(shortcut, &component->current->shortcuts, link) {
        if (shortcut->is_present) {
            is_active = true;
            break;
        }
    }

    return sd_bus_reply_method_return(msg, "b", is_active);
}

// SD_BUS_METHOD("shortcutNames", "s", "as", shortcut_names_ex, 0),
static int shortcut_names(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *context_unique = NULL;
    CK(sd_bus_message_read(msg, "s", &context_unique));

    struct global_shortcut_component *component = userdata;
    struct global_shortcut_context *context = global_shortcut_component_get_context(
        component, context_unique ? context_unique : "default");

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(msg, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "s"));

    if (context) {
        struct global_shortcut *shortcut;
        wl_list_for_each(shortcut, &context->shortcuts, link) {
            CK(sd_bus_message_append(reply, "s", shortcut->unique_name));
        }
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static const sd_bus_vtable kglobalaccel_component_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_SIGNAL("globalShortcutPressed", "ssx", 0),
    SD_BUS_PROPERTY("friendlyName", "s", friendly_name, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("uniqueName", "s", unique_name, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("allShortcutInfos", "s", "a(ssssssaiai)", all_shortcut_infos, 0),
    SD_BUS_METHOD("cleanUp", "", "b", clean_up, 0),
    SD_BUS_METHOD("getShortcutContexts", "", "as", get_shortcut_contexts, 0),
    SD_BUS_METHOD("invokeShortcut", "ss", "", invoke_shortcut, 0),
    SD_BUS_METHOD("isActive", "", "b", is_active, 0),
    SD_BUS_METHOD("shortcutNames", "s", "as", shortcut_names, 0),
    SD_BUS_VTABLE_END,
};

static const char *get_dbus_path(const char *name)
{
    int len = snprintf(NULL, 0, "%s/%s", component_path_prefix, name) + 1;
    char *path = malloc(len);
    if (!path) {
        return NULL;
    }

    snprintf(path, len, "%s/%s", component_path_prefix, name);

    /* DBus path can only contain ASCII characters */
    char *p = path + strlen(component_path_prefix) + 1;
    for (; *p; ++p) {
        if (!isalnum(*p) || (*p & ~0x7f) != 0) {
            *p = '_';
        }
    }

    return path;
}

static struct global_shortcut_component *global_shortcut_component_create(const char *unique_name,
                                                                          const char *friendly_name)
{
    struct global_shortcut_component *component =
        calloc(1, sizeof(struct global_shortcut_component));
    if (!component) {
        return NULL;
    }

    wl_list_init(&component->contexts);
    wl_list_insert(&registry->components, &component->link);

    component->unique_name = strdup(unique_name);
    component->friendly_name = strdup(friendly_name);

    /* create the default context in the component */
    component->current = global_shortcut_context_create(component, "default", "Default Context");

    /* register component dbus */
    component->dbus_path = get_dbus_path(unique_name);
    component->config =
        config_manager_add_config(NULL, NULL, component->dbus_path, component_interface,
                                  kglobalaccel_component_vtable, component);
    return component;
}

static void global_shortcut_component_destroy(struct global_shortcut_component *component)
{
    wl_list_remove(&component->link);

    free(component->unique_name);
    free(component->friendly_name);
    free((void *)component->dbus_path);

    struct global_shortcut_context *context, *tmp;
    wl_list_for_each_safe(context, tmp, &component->contexts, link) {
        global_shortcut_context_destroy(context);
    }

    free(component);
}

/**
 * dbus process fuctions for org.kde.KGlobalAccel
 */

// SD_BUS_METHOD("actionList", "(ai)", "as", action_list, 0),
static int action_list(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    int32_t key;
    CK(sd_bus_message_read(msg, "(ai)", 4, &key, NULL, NULL, NULL));

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(msg, &reply));

    struct global_shortcut *shortcut = global_shortcut_registry_get_shortcut_by_key(key);
    if (shortcut) {
        CK(sd_bus_message_append(reply, "as", 4, shortcut->context->component->unique_name,
                                 shortcut->unique_name, shortcut->context->component->friendly_name,
                                 shortcut->friendly_name));
    }

    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

// SD_BUS_METHOD("activateGlobalShortcutContext", "ss", "", activate_global_shortcut_context, 0),
static int activate_global_shortcut_context(sd_bus_message *msg, void *userdata,
                                            sd_bus_error *ret_error)
{
    const char *component_unique, *context_unique;
    CK(sd_bus_message_read(msg, "ss", &component_unique, &context_unique));

    struct global_shortcut_component *component =
        global_shortcut_registry_get_component(component_unique);
    if (component) {
        struct global_shortcut_context *context =
            global_shortcut_component_get_context(component, context_unique);
        if (context) {
            struct global_shortcut *shortcut;
            wl_list_for_each(shortcut, &component->current->shortcuts, link) {
                global_shortcut_set_inactive(shortcut);
            }
            component->current = context;
        }
    }

    return sd_bus_reply_method_return(msg, NULL);
}

// SD_BUS_METHOD("allActionsForComponent", "as", "aas", all_actions_for_component, 0),
static int all_actions_for_component(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *component_unique;
    /* only need component unique name in action_id */
    CK(sd_bus_message_read(msg, "as", 4, &component_unique, NULL, NULL, NULL));

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(msg, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "as"));

    struct global_shortcut_component *component =
        global_shortcut_registry_get_component(component_unique);
    if (component) {
        struct global_shortcut_context *context =
            global_shortcut_component_get_context(component, "default");
        struct global_shortcut *shortcut;
        wl_list_for_each(shortcut, &context->shortcuts, link) {
            if (shortcut->is_fresh) {
                continue;
            }
            CK(sd_bus_message_append(reply, "as", 4, component->unique_name, shortcut->unique_name,
                                     component->friendly_name, shortcut->friendly_name));
        }
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

// SD_BUS_METHOD("allComponents", "", "ao", all_components, 0),
static int all_components(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(msg, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "o"));

    struct global_shortcut_component *component;
    wl_list_for_each(component, &registry->components, link) {
        CK(sd_bus_message_append_basic(reply, 'o', component->dbus_path));
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

// SD_BUS_METHOD("allMainComponents", "", "aas", all_main_components, 0),
static int all_main_components(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(msg, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "as"));

    struct global_shortcut_component *component;
    wl_list_for_each(component, &registry->components, link) {
        CK(sd_bus_message_append(reply, "as", 4, component->unique_name, "",
                                 component->friendly_name, ""));
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

// SD_BUS_METHOD("blockGlobalShortcuts", "b", "", block_global_shortcuts, 0),
static int block_global_shortcuts(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    uint32_t block;
    CK(sd_bus_message_read(msg, "b", &block));

    /* activate and deactivate all shortcuts in component current context */
    struct global_shortcut_component *component;
    wl_list_for_each(component, &registry->components, link) {
        struct global_shortcut *shortcut;
        wl_list_for_each(shortcut, &component->current->shortcuts, link) {
            block ? global_shortcut_set_inactive(shortcut) : global_shortcut_set_active(shortcut);
        }
    }

    return sd_bus_reply_method_return(msg, NULL);
}

// SD_BUS_METHOD("defaultShortcutKeys", "as", "a(ai)", default_shortcut_keys, 0),
static int default_shortcut_keys(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *component_unique, *action_unique;
    CK(sd_bus_message_read(msg, "as", 4, &component_unique, &action_unique, NULL, NULL));

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(msg, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(ai)"));

    struct global_shortcut *shortcut =
        global_shortcut_registry_get_shortcut_by_name(component_unique, action_unique);
    if (shortcut) {
        CK(sd_bus_message_append(reply, "(ai)", 4, shortcut->default_key, 0, 0, 0));
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

// SD_BUS_METHOD("doRegister", "as", "", do_register, 0),
static int do_register(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *component_unique, *action_unique, *component_friendly, *action_friendly;
    CK(sd_bus_message_read(msg, "as", 4, &component_unique, &action_unique, &component_friendly,
                           &action_friendly));

    struct global_shortcut *shortcut =
        global_shortcut_registry_get_shortcut_by_name(component_unique, action_unique);
    if (shortcut) {
        /* replace friendly names */
        struct global_shortcut_component *componet = shortcut->context->component;
        if (*component_friendly && strcmp(componet->friendly_name, component_friendly)) {
            free(componet->friendly_name);
            componet->friendly_name = strdup(component_friendly);
        }
        if (*action_friendly && strcmp(shortcut->friendly_name, action_friendly)) {
            free(shortcut->friendly_name);
            shortcut->friendly_name = strdup(action_friendly);
        }
    } else {
        /* create a shortcut */
        struct global_shortcut_component *component =
            global_shortcut_registry_get_component(component_unique);
        /*  Create the component if necessary */
        if (!component) {
            component = global_shortcut_component_create(component_unique, component_friendly);
        }
        global_shortcut_create(component->current, action_unique, action_friendly);
    }

    return sd_bus_reply_method_return(msg, NULL);
}

// SD_BUS_METHOD("getComponent", "s", "o", get_component, 0),
static int get_component(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *component_unique;
    CK(sd_bus_message_read(msg, "s", &component_unique));

    struct global_shortcut_component *component =
        global_shortcut_registry_get_component(component_unique);
    if (!component) {
        const sd_bus_error error = SD_BUS_ERROR_MAKE_CONST("org.kde.kglobalaccel.NoSuchComponent",
                                                           "The component doesn't exist.");
        return sd_bus_reply_method_error(msg, &error);
    }

    return sd_bus_reply_method_return(msg, "o", component->dbus_path);
}

// SD_BUS_METHOD("globalShortcutsByKey", "(ai)i", "a(ssssssaiai)", global_shortcuts_by_key, 0),
static int global_shortcuts_by_key(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    int32_t key, type;
    CK(sd_bus_message_read(msg, "(ai)i", 4, &key, NULL, NULL, NULL, &type));

    struct global_shortcut_component *component;
    struct global_shortcut_context *context;
    struct global_shortcut *shortcut;

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(msg, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(ssssssaiai)"));

    wl_list_for_each(component, &registry->components, link) {
        wl_list_for_each(context, &component->contexts, link) {
            shortcut = global_shortcut_context_get_shortcut_by_key(context, key);
            if (shortcut) {
                CK(sd_bus_message_append(reply, "(ssssssaiai)", context->unique_name,
                                         context->friendly_name, component->unique_name,
                                         component->friendly_name, shortcut->unique_name,
                                         shortcut->friendly_name, 4, shortcut->key, 0, 0, 0, 4,
                                         shortcut->default_key, 0, 0, 0));
            }
        }
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

// SD_BUS_METHOD("globalShortcutAvailable", "(ai)s", "b", global_shortcut_available, 0),
static int global_shortcut_available(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *component_unique;
    int32_t key;

    CK(sd_bus_message_read(msg, "(ai)s", 4, &key, NULL, NULL, NULL, &component_unique));

    struct global_shortcut *shortcut = NULL;
    struct global_shortcut_component *component =
        global_shortcut_registry_get_component(component_unique);
    if (component) {
        struct global_shortcut_context *context;
        wl_list_for_each(context, &component->contexts, link) {
            shortcut = global_shortcut_context_get_shortcut_by_key(context, key);
            if (shortcut) {
                break;
            }
        }
    }

    return sd_bus_reply_method_return(msg, "b", !!shortcut);
}

// SD_BUS_METHOD("setForeignShortcutKeys", "asa(ai)", "", set_foreign_shortcut_keys, 0),
static int set_foreign_shortcut_keys(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *component_unique, *action_unique;
    int32_t key;

    CK(sd_bus_message_read(msg, "as", 4, &component_unique, &action_unique, NULL, NULL));
    CK(sd_bus_message_enter_container(msg, 'a', "(ai)"));
    CK(sd_bus_message_read(msg, "(ai)", 4, &key, NULL, NULL, NULL));
    // TODO: check key list: sd_bus_message_read(msg, "(ai)", 4, NULL, NULL, NULL, NULL) == 0
    CK(sd_bus_message_exit_container(msg));

    struct global_shortcut *shortcut =
        global_shortcut_registry_get_shortcut_by_name(component_unique, action_unique);
    if (shortcut) {
        shortcut->key = key;
        sd_bus_emit_signal(sd_bus_slot_get_bus(registry->config->slot), registry_path,
                           registry_interface, "yourShortcutsChanged", "asa(ai)", 4,
                           shortcut->context->component->unique_name, shortcut->unique_name,
                           shortcut->context->component->friendly_name, shortcut->friendly_name, 1,
                           4, shortcut->key, 0, 0, 0);
    }

    return sd_bus_reply_method_return(msg, NULL);
}

// SD_BUS_METHOD("setInactive", "as", "", set_inactive, 0),
static int set_inactive(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *component_unique, *action_unique;
    CK(sd_bus_message_read(msg, "as", 4, &component_unique, &action_unique, NULL, NULL));

    struct global_shortcut *shortcut =
        global_shortcut_registry_get_shortcut_by_name(component_unique, action_unique);
    if (shortcut) {
        shortcut->is_present = false;
        global_shortcut_set_inactive(shortcut);
    }

    return sd_bus_reply_method_return(msg, NULL);
}

// SD_BUS_METHOD("setShortcutKeys", "asa(ai)u", "a(ai)", set_shortcut_keys, 0),
static int set_shortcut_keys(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *component_unique, *action_unique;
    int32_t key;
    uint32_t flags;

    CK(sd_bus_message_read(msg, "as", 4, &component_unique, &action_unique, NULL, NULL));
    CK(sd_bus_message_enter_container(msg, 'a', "(ai)"));
    CK(sd_bus_message_read(msg, "(ai)", 4, &key, NULL, NULL, NULL));
    // TODO: check key list: sd_bus_message_read(msg, "(ai)", 4, NULL, NULL, NULL, NULL) == 0
    CK(sd_bus_message_exit_container(msg));
    CK(sd_bus_message_read(msg, "u", &flags));

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(msg, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(ai)"));

    struct global_shortcut *shortcut =
        global_shortcut_registry_get_shortcut_by_name(component_unique, action_unique);
    if (shortcut) {
        bool setPresent = (flags & SetPresent);
        bool isAutoloading = !(flags & NoAutoloading);
        bool isDefault = (flags & IsDefault);

        // default shortcuts cannot clash because they don't do anything
        if (isDefault) {
            shortcut->default_key = key;
            CK(sd_bus_message_append(reply, "(ai)", 4, key, 0, 0, 0));
            goto out;
        }
        if (isAutoloading && !shortcut->is_fresh) {
            if (!shortcut->is_present && setPresent) {
                shortcut->is_present = true;
                global_shortcut_set_active(shortcut);
            }
            // We are finished here. Return the list of current active keys.
            CK(sd_bus_message_append(reply, "(ai)", 4, shortcut->key, 0, 0, 0));
            goto out;
        }

        // now we are actually changing the shortcut of the action
        shortcut->key = key;
        global_shortcut_create_binding(shortcut);

        if (setPresent) {
            shortcut->is_present = true;
            global_shortcut_set_active(shortcut);
        }

        shortcut->is_fresh = false;
        CK(sd_bus_message_append(reply, "(ai)", 4, shortcut->key, 0, 0, 0));
    }

out:
    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

// SD_BUS_METHOD("shortcutKeys", "as", "a(ai)", shortcut_keys, 0),
static int shortcut_keys(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *component_unique, *action_unique;
    CK(sd_bus_message_read(msg, "as", 4, &component_unique, &action_unique, NULL, NULL));

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(msg, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(ai)"));

    struct global_shortcut *shortcut =
        global_shortcut_registry_get_shortcut_by_name(component_unique, action_unique);
    if (shortcut) {
        CK(sd_bus_message_append(reply, "(ai)", 4, shortcut->key, 0, 0, 0));
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

// SD_BUS_METHOD("unregister", "ss", "b", un_register, 0),
static int un_register(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    const char *component_unique, *shortcut_unique;
    CK(sd_bus_message_read(msg, "ss", &component_unique, &shortcut_unique));

    struct global_shortcut *shortcut =
        global_shortcut_registry_get_shortcut_by_name(component_unique, shortcut_unique);
    if (shortcut) {
        global_shortcut_destroy(shortcut, true);
    }

    return sd_bus_reply_method_return(msg, "b", !!shortcut);
}

/* only support kglobalaccel >= 5.90 */
static const sd_bus_vtable kglobalaccel_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_SIGNAL("yourShortcutsChanged", "asa(ai)", 0),
    SD_BUS_METHOD("actionList", "(ai)", "as", action_list, 0),
    SD_BUS_METHOD("activateGlobalShortcutContext", "ss", "", activate_global_shortcut_context, 0),
    SD_BUS_METHOD("allActionsForComponent", "as", "aas", all_actions_for_component, 0),
    SD_BUS_METHOD("allComponents", "", "ao", all_components, 0),
    SD_BUS_METHOD("allMainComponents", "", "aas", all_main_components, 0),
    SD_BUS_METHOD("blockGlobalShortcuts", "b", "", block_global_shortcuts, 0),
    SD_BUS_METHOD("defaultShortcutKeys", "as", "a(ai)", default_shortcut_keys, 0),
    SD_BUS_METHOD("doRegister", "as", "", do_register, 0),
    SD_BUS_METHOD("getComponent", "s", "o", get_component, 0),
    SD_BUS_METHOD("globalShortcutAvailable", "(ai)s", "b", global_shortcut_available, 0),
    SD_BUS_METHOD("globalShortcutsByKey", "(ai)i", "a(ssssssaiai)", global_shortcuts_by_key, 0),
    SD_BUS_METHOD("setForeignShortcutKeys", "asa(ai)", "", set_foreign_shortcut_keys, 0),
    SD_BUS_METHOD("setInactive", "as", "", set_inactive, 0),
    SD_BUS_METHOD("setShortcutKeys", "asa(ai)u", "a(ai)", set_shortcut_keys, 0),
    SD_BUS_METHOD("shortcutKeys", "as", "a(ai)", shortcut_keys, 0),
    SD_BUS_METHOD("unregister", "ss", "b", un_register, 0),
    SD_BUS_VTABLE_END,
};

static void handle_config_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&registry->destroy.link);

    /* free all components contexts shorcuts */
    struct global_shortcut_component *component, *tmp;
    wl_list_for_each_safe(component, tmp, &registry->components, link) {
        global_shortcut_component_destroy(component);
    }

    free(registry);
    registry = NULL;
}

bool kde_global_accel_manager_create(struct server *server)
{
    registry = calloc(1, sizeof(struct global_shortcut_registry));
    if (!registry) {
        return false;
    }

    registry->config = config_manager_add_config(NULL, registry_bus, registry_path,
                                                 registry_interface, kglobalaccel_vtable, registry);
    if (!registry->config) {
        free(registry);
        registry = NULL;
        return false;
    }

    wl_list_init(&registry->components);

    registry->destroy.notify = handle_config_destroy;
    wl_signal_add(&registry->config->events.destroy, &registry->destroy);

    return true;
}
