// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECT_MOVE_H_
#define _EFFECT_MOVE_H_

#include "view/view.h"

struct move_proxy *move_proxy_create(struct view *view, int width, int height);

void move_proxy_destroy(struct move_proxy *proxy);

void move_proxy_add_destroy_listener(struct move_proxy *proxy, struct wl_listener *listener);

void move_proxy_move(struct move_proxy *proxy, int x, int y);

void move_proxy_resize(struct move_proxy *proxy, int width, int height);

#endif /* _EFFECT_MOVE_H_ */
