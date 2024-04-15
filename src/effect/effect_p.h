// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECT_P_H_
#define _EFFECT_P_H_

#include "effect/effect.h"
#include "server.h"

bool capture_manager_create(struct server *server);

bool ky_capture_manager_create(struct server *server);

#endif /* _EFFECT_P_H_ */
