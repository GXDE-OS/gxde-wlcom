// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include "effect_p.h"
#include "scene/animation.h"
#include "scene/thumbnail.h"

bool effect_manager_create(struct server *server)
{
    animation_manager_create(server);
    thumbnail_manager_create(server);

    capture_manager_create(server);
    ky_capture_manager_create(server);

    return true;
}
