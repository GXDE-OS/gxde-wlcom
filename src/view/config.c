// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include "config.h"
#include "view/workspace.h"
#include "view_p.h"

bool view_manager_config_init(struct view_manager *view_manager)
{
    view_manager->config = config_manager_add_config("Views", NULL, NULL, NULL, NULL, view_manager);
    return !!view_manager->config;
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

    return true;
}

void view_write_config(struct view_manager *view_manager)
{
    if (!view_manager->config) {
        return;
    }

    json_object_object_add(view_manager->config->json, "num_workspaces",
                           json_object_new_int(workspace_manager_get_count()));
}
