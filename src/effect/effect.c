// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include "effect_p.h"

bool effect_manager_create(struct server *server)
{
    screencopy_manager_create(server);

    return true;
}
