#include <strings.h>
#include <xkbcommon/xkbcommon.h>

#include <kywc/log.h>

#include "input.h"

static struct modifier {
    char *name;
    uint32_t mod;
} modifiers[] = {
    { XKB_MOD_NAME_SHIFT, KYWC_MODIFIER_SHIFT },
    { XKB_MOD_NAME_CAPS, KYWC_MODIFIER_CAPS },
    { "Ctrl", KYWC_MODIFIER_CTRL },
    { XKB_MOD_NAME_CTRL, KYWC_MODIFIER_CTRL },
    { "Alt", KYWC_MODIFIER_ALT },
    { XKB_MOD_NAME_ALT, KYWC_MODIFIER_ALT },
    { XKB_MOD_NAME_NUM, KYWC_MODIFIER_MOD2 },
    { "Mod3", KYWC_MODIFIER_MOD3 },
    { "Win", KYWC_MODIFIER_LOGO },
    { XKB_MOD_NAME_LOGO, KYWC_MODIFIER_LOGO },
    { "Mod5", KYWC_MODIFIER_MOD5 },
};

uint32_t keyboard_get_modifier_mask_by_name(const char *name)
{
    for (int i = 0; i < (int)(sizeof(modifiers) / sizeof(struct modifier)); i++) {
        if (strcasecmp(modifiers[i].name, name) == 0) {
            return modifiers[i].mod;
        }
    }
    return 0;
}

const char *keyboard_get_modifier_name_by_mask(uint32_t modifier)
{
    for (int i = 0; i < (int)(sizeof(modifiers) / sizeof(struct modifier)); i++) {
        if (modifiers[i].mod == modifier) {
            return modifiers[i].name;
        }
    }

    return NULL;
}

static int get_modifier_names(const char **names, uint32_t modifier_masks)
{
    int length = 0;
    for (int i = 0; i < (int)(sizeof(modifiers) / sizeof(struct modifier)); i++) {
        if ((modifier_masks & modifiers[i].mod) != 0) {
            names[length] = modifiers[i].name;
            ++length;
            modifier_masks ^= modifiers[i].mod;
        }
    }

    return length;
}

static void modifiers_mask_debug(uint32_t mask, const char *mask_name)
{
    const char *names[8];
    char name[64];
    char *p = name;

    int len = get_modifier_names(names, mask);
    for (int i = 0; i < len; i++) {
        p += sprintf(p, "%s ", names[i]);
    }
    if (len > 0) {
        *--p = '\0'; // strip last space
        kywc_log(KYWC_DEBUG, "\t %s: %s", mask_name, name);
    }
}

// TODO: compose and dead key support
static void keyboard_state_erase_key(struct keyboard_state *keyboard_state, uint32_t keysym)
{
    uint32_t idx = 0;
    for (size_t i = 0; i < keyboard_state->npressed; i++) {
        if (i > idx) {
            keyboard_state->pressed_keysyms[idx] = keyboard_state->pressed_keysyms[i];
        }
        if (keyboard_state->pressed_keysyms[i] != keysym) {
            idx++;
        }
    }

    while (keyboard_state->npressed > idx) {
        keyboard_state->npressed--;
        keyboard_state->pressed_keysyms[keyboard_state->npressed] = 0;
    }
    kywc_log(KYWC_DEBUG, "erase key %d, %lu", keysym, keyboard_state->npressed);
    for (size_t i = 0; i < keyboard_state->npressed; i++) {
        kywc_log(KYWC_DEBUG, "\t current keysym %lu: %d", i, keyboard_state->pressed_keysyms[i]);
    }
}

static void keyboard_state_add_key(struct keyboard_state *keyboard_state, uint32_t keysym)
{
    if (keyboard_state->npressed >= MAX_PRESSED_KEY) {
        return;
    }

    keyboard_state->pressed_keysyms[keyboard_state->npressed] = keysym;
    keyboard_state->npressed++;
    kywc_log(KYWC_DEBUG, "add key %d, %lu", keysym, keyboard_state->npressed);
    for (size_t i = 0; i < keyboard_state->npressed; i++) {
        kywc_log(KYWC_DEBUG, "\t current keysym %lu: %d", i, keyboard_state->pressed_keysyms[i]);
    }
}

static void handle_keyboard_state(struct keyboard_state *keyboard_state, uint32_t modifiers,
                                  uint32_t keysym, bool pressed)
{

    bool last_key_is_modifiers = modifiers != keyboard_state->last_modifiers;
    keyboard_state->last_modifiers = modifiers;

    if (last_key_is_modifiers && keyboard_state->last_keysym) {
        // a modifiier key preesed before this key, erase it
        keyboard_state_erase_key(keyboard_state, keyboard_state->last_keysym);
        keyboard_state->last_keysym = 0;
    }

    if (pressed) {
        keyboard_state_add_key(keyboard_state, keysym);
        keyboard_state->last_keysym = keysym;
    } else {
        keyboard_state_erase_key(keyboard_state, xkb_keysym_to_upper(keysym));
        keyboard_state_erase_key(keyboard_state, xkb_keysym_to_lower(keysym));
    }
}

static bool keyboard_handle_bindings(struct keyboard *keyboard, uint32_t key, bool pressed,
                                     uint32_t modifiers)
{
    struct keyboard_state *keyboard_state = &keyboard->state;

    /* Translate libinput keycode -> xkbcommon keysym */
    const xkb_keysym_t *keysyms;
    size_t keysyms_len = xkb_state_key_get_syms(keyboard_state->xkb_state, key + 8, &keysyms);

    for (size_t i = 0; i < keysyms_len; ++i) {
        handle_keyboard_state(keyboard_state, modifiers, keysyms[i], pressed);
    }

    // if (check_vt_switch(raw_key, modifiers)) {
    //    return true;
    // }

    if (pressed) {
        return bindings_handle_key_binding(keyboard_state);
    }
    return false;
}

void keyboard_feed_key(struct keyboard *keyboard, uint32_t key, bool pressed, uint32_t time,
                       uint32_t modifiers)
{
    kywc_log(KYWC_DEBUG, "keyboard keycode %d %s", key, pressed ? "pressed" : "released");

    modifiers_mask_debug(modifiers, "modifiers");
    keyboard_handle_bindings(keyboard, key, pressed, modifiers);
}

void keyboard_feed_modifiers(struct keyboard *keyboard, uint32_t depressed, uint32_t latched,
                             uint32_t locked, uint32_t group)
{
    if (kywc_log_get_level() == KYWC_DEBUG) {
        kywc_log(KYWC_DEBUG, "keyboard modifiers update");
        modifiers_mask_debug(depressed, "depressed");
        modifiers_mask_debug(latched, "latched");
        modifiers_mask_debug(locked, "locked");
        modifiers_mask_debug(group, "group");
    }
}
