// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include "config_p.h"
#include "theme.h"

enum KylinTheme { Light = 0, Dark, Default };

enum KylinIconTheme { IconThemeDefault = 0, IconThemeFashion, IconThemeClassical };

static int slot_theme_change(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    int32_t theme = Default;
    sd_bus_message_read_basic(msg, 'i', &theme);

    switch (theme) {
    case Light:
    case Default:
        theme_manager_set_theme(THEME_TYPE_LIGHT);
        break;
    case Dark:
        theme_manager_set_theme(THEME_TYPE_DARK);
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

static int slot_icon_theme_change(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    int32_t theme = IconThemeDefault;
    sd_bus_message_read_basic(msg, 'i', &theme);

    switch (theme) {
    case IconThemeDefault:
        theme_manager_set_icon_theme("ukui-icon-theme-default");
        break;
    case IconThemeFashion:
        theme_manager_set_icon_theme("ukui-icon-theme-fashion");
        break;
    case IconThemeClassical:
        theme_manager_set_icon_theme("ukui-icon-theme-classical");
        break;
    }

    return 0;
}

bool kde_global_settings_create(struct config_manager *config_manager)
{
    sd_bus_match_signal(config_manager->bus, NULL, NULL, "/KGlobalSettings",
                        "org.kde.KGlobalSettings", "slotThemeChange", slot_theme_change, NULL);

    sd_bus_match_signal(config_manager->bus, NULL, NULL, "/KGlobalSettings",
                        "org.kde.KGlobalSettings", "slotFontChange", slot_font_change, NULL);

    sd_bus_match_signal(config_manager->bus, NULL, NULL, "/KGlobalSettings",
                        "org.kde.KGlobalSettings", "slotIconThemeChange", slot_icon_theme_change,
                        NULL);

    return true;
}
