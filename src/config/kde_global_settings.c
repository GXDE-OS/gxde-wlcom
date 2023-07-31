#include "config_p.h"
#include "theme.h"

enum KylinThemeMode { Light, Dark, Default };

static int slot_theme_change(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    int32_t theme = Default;
    sd_bus_message_read_basic(msg, 'i', &theme);

    switch (theme) {
    case Light:
    case Default:
        theme_manager_set_theme("builtin-light");
        break;
    case Dark:
        theme_manager_set_theme("builtin-dark");
        break;
    }
    return 0;
}

static int slot_font_change(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    int32_t size;
    char *name;
    sd_bus_message_read(msg, "is", &size, &name);

    theme_manager_set_font(name, size);
    return 0;
}

bool kde_global_settings_create(struct config_manager *config_manager)
{
    sd_bus_match_signal(config_manager->bus, NULL, NULL, "/KGlobalSettings",
                        "org.kde.KGlobalSettings", "slotThemeChange", slot_theme_change, NULL);

    sd_bus_match_signal(config_manager->bus, NULL, NULL, "/KGlobalSettings",
                        "org.kde.KGlobalSettings", "slotFontChange", slot_font_change, NULL);

    return true;
}
