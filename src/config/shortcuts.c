#include <stdlib.h>

#include <kywc/binding.h>

#include "config_p.h"
#include "util/spawn.h"

static struct shortcut {
    char *keybind;
    char *command;
    char *desc;
} default_shortcuts[] = {
    { "Ctrl+Alt+t", "gnome-terminal", "open ternimal" },
};

#define default_shortcuts_count (sizeof(default_shortcuts) / sizeof(struct shortcut))

static void shortcut_action(struct key_binding *binding, void *data)
{
    struct shortcut *shortcut = data;
    spawn_invoke(shortcut->command);
}

void shortcut_init(void)
{
    for (size_t i = 0; i < default_shortcuts_count; i++) {
        struct shortcut *shortcut = &default_shortcuts[i];
        struct key_binding *binding = kywc_key_binding_create(shortcut->keybind, shortcut->desc);
        if (!binding) {
            continue;
        }

        if (!kywc_key_binding_register(binding, shortcut_action, shortcut)) {
            kywc_key_binding_destroy(binding);
            continue;
        }
    }
}
