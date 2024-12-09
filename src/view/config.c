// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include "config.h"
#include "util/dbus.h"
#include "view/workspace.h"
#include "view_p.h"

static const char *service_path = "/com/kylin/Wlcom/View";
static const char *service_interface = "com.kylin.Wlcom.View";

static int get_view_adsorption(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct view_manager *manager = userdata;
    return sd_bus_reply_method_return(m, "u", manager->state.view_adsorption);
}

static int set_view_adsorption(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct view_manager *manager = userdata;
    uint32_t adsorption;
    CK(sd_bus_message_read(m, "u", &adsorption));

    if (adsorption > VIEW_ADSORPTION_ALL) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid adsorption.");
        return sd_bus_reply_method_error(m, &error);
    }

    manager->state.view_adsorption = adsorption;

    return sd_bus_reply_method_return(m, NULL);
}

static int set_csd_round_corner(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct view_manager *manager = userdata;
    uint32_t enabled;
    CK(sd_bus_message_read(m, "b", &enabled));

    if (manager->state.csd_round_corner != enabled) {
        manager->state.csd_round_corner = enabled;

        struct view *view;
        wl_list_for_each(view, &manager->views, link) {
            if (view->base.ssd == KYWC_SSD_NONE) {
                view_update_round_corner(view);
            }
        }
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int set_minimize_effect(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct view_manager *manager = userdata;
    uint32_t minimize_effect;
    CK(sd_bus_message_read(m, "u", &minimize_effect));

    if (minimize_effect < MINIMIZE_EFFECT_TYPE_NUM) {
        manager->state.minimize_effect_type = minimize_effect;
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int list_all_views(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct view_manager *manager = userdata;
    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(ssi)"));

    struct view *view;
    wl_list_for_each(view, &manager->views, link) {
        if (!view->base.mapped) {
            continue;
        }
        const char *app_id = view->base.app_id;
        const char *uuid = view->base.uuid;
        int pid = view->pid;
        sd_bus_message_append(reply, "(ssi)", app_id, uuid, pid);
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static int list_view_states(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *uuid;
    CK(sd_bus_message_read(m, "s", &uuid));

    struct kywc_view *kywc_view = kywc_view_by_uuid(uuid);
    if (!kywc_view) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid uuid.");
        return sd_bus_reply_method_error(m, &error);
    }

    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(sai)"));

    sd_bus_message_append(reply, "(sai)", "role", 1, kywc_view->role);
    sd_bus_message_append(reply, "(sai)", "geometry", 4, kywc_view->geometry.x,
                          kywc_view->geometry.y, kywc_view->geometry.width,
                          kywc_view->geometry.height);
    sd_bus_message_append(reply, "(sai)", "margin", 4, kywc_view->margin.off_x,
                          kywc_view->margin.off_y, kywc_view->margin.off_width,
                          kywc_view->margin.off_height);
    sd_bus_message_append(reply, "(sai)", "padding", 4, kywc_view->padding.top,
                          kywc_view->padding.bottom, kywc_view->padding.left,
                          kywc_view->padding.right);
    sd_bus_message_append(reply, "(sai)", "ssd", 1, kywc_view->ssd);
    sd_bus_message_append(reply, "(sai)", "minimize size", 2, kywc_view->min_width,
                          kywc_view->min_height);
    sd_bus_message_append(reply, "(sai)", "maximize size", 2, kywc_view->max_width,
                          kywc_view->max_height);
    sd_bus_message_append(reply, "(sai)", "kept_above", 1, kywc_view->kept_above);
    sd_bus_message_append(reply, "(sai)", "kept_below", 1, kywc_view->kept_below);
    sd_bus_message_append(reply, "(sai)", "minimized", 1, kywc_view->minimized);
    sd_bus_message_append(reply, "(sai)", "maximized", 1, kywc_view->maximized);
    sd_bus_message_append(reply, "(sai)", "fullscreen", 1, kywc_view->fullscreen);
    sd_bus_message_append(reply, "(sai)", "activated", 1, kywc_view->activated);
    sd_bus_message_append(reply, "(sai)", "tiled", 1, kywc_view->tiled);
    sd_bus_message_append(reply, "(sai)", "modal", 1, kywc_view->modal);
    sd_bus_message_append(reply, "(sai)", "skip_taskbar", 1, kywc_view->skip_taskbar);
    sd_bus_message_append(reply, "(sai)", "skip_switcher", 1, kywc_view->skip_switcher);

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

static int list_all_modes(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct view_manager *manager = userdata;
    sd_bus_message *reply = NULL;
    CK(sd_bus_message_new_method_return(m, &reply));
    CK(sd_bus_message_open_container(reply, 'a', "(sb)"));

    struct view_mode *mode;
    wl_list_for_each(mode, &manager->view_modes, link) {
        sd_bus_message_append(reply, "(sb)", mode->impl->name, mode == manager->mode);
    }

    CK(sd_bus_message_close_container(reply));
    CK(sd_bus_send(NULL, reply, NULL));
    sd_bus_message_unref(reply);
    return 1;
}

#if 0
static int set_view_mode(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    const char *name;
    CK(sd_bus_message_read(m, "s", &name));

    if (!view_manager_set_view_mode(name)) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid mode name.");
        return sd_bus_reply_method_error(m, &error);
    }

    return sd_bus_reply_method_return(m, NULL);
}
#endif

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetViewAdsorption", "", "u", get_view_adsorption, 0),
    SD_BUS_METHOD("SetViewAdsorption", "u", "", set_view_adsorption, 0),
    SD_BUS_METHOD("SetCSDRoundCorner", "b", "", set_csd_round_corner, 0),
    SD_BUS_METHOD("SetMinimizeEffect", "u", "", set_minimize_effect, 0),
    SD_BUS_METHOD("ListAllViews", "", "a(ssi)", list_all_views, 0),
    SD_BUS_METHOD("ListViewStates", "s", "a(sai)", list_view_states, 0),

    SD_BUS_METHOD("ListAllModes", "", "a(sb)", list_all_modes, 0),
    // SD_BUS_METHOD("SetViewMode", "s", "", set_view_mode, 0),
    SD_BUS_VTABLE_END,
};

bool view_manager_config_init(struct view_manager *view_manager)
{
    view_manager->config = config_manager_add_config("Views");
    if (!view_manager->config) {
        return false;
    }
    return dbus_register_object(NULL, service_path, service_interface, service_vtable,
                                view_manager);
}

bool view_read_config(struct view_manager *view_manager)
{
    if (!view_manager->config || !view_manager->config->json) {
        return false;
    }

    json_object *data;
    if (json_object_object_get_ex(view_manager->config->json, "num_workspaces", &data)) {
        view_manager->state.num_workspaces = json_object_get_int(data);
    }
    if (json_object_object_get_ex(view_manager->config->json, "view_adsorption", &data)) {
        view_manager->state.view_adsorption = json_object_get_int(data);
    }
    if (json_object_object_get_ex(view_manager->config->json, "csd_round_corner", &data)) {
        view_manager->state.csd_round_corner = json_object_get_boolean(data);
    }
    if (json_object_object_get_ex(view_manager->config->json, "view_mode", &data)) {
        const char *name = json_object_get_string(data);
        view_manager->mode = view_manager_mode_from_name(name);
    }
    if (json_object_object_get_ex(view_manager->config->json, "minimize_effect", &data)) {
        int minimize_effect = json_object_get_int(data);
        if (minimize_effect < MINIMIZE_EFFECT_TYPE_NUM) {
            view_manager->state.minimize_effect_type = minimize_effect;
        }
    }

    return true;
}

void view_write_config(struct view_manager *view_manager)
{
    if (!view_manager->config) {
        return;
    }

    json_object_object_add(view_manager->config->json, "num_workspaces",
                           json_object_new_int(workspace_manager_get_count()));
    json_object_object_add(view_manager->config->json, "view_adsorption",
                           json_object_new_int(view_manager->state.view_adsorption));
    json_object_object_add(view_manager->config->json, "csd_round_corner",
                           json_object_new_boolean(view_manager->state.csd_round_corner));
    json_object_object_add(view_manager->config->json, "view_mode",
                           json_object_new_string(view_manager->mode->impl->name));
    json_object_object_add(view_manager->config->json, "minimize_effect",
                           json_object_new_int(view_manager->state.minimize_effect_type));
}
