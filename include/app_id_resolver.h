/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This file is part of gxde-wlcom.
 *
 * gxde-wlcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gxde-wlcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gxde-wlcom.  If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------
 * The ported implementation for Treeland's treeland-app-id-resolver-v1.
 */

#ifndef APP_ID_RESOLVER_H
#define APP_ID_RESOLVER_H

#include <stdbool.h>

struct server;
struct app_id_resolver_manager;

/* Callback for app id resolution requests */
typedef void (*app_id_resolver_cb)(const char *app_id, void *user_data);


/* Constructs treeland_app_id_resolver_manager_v1 global */
struct app_id_resolver_manager *app_id_resolver_manager_create(struct server *server);

/* Get the manager singleton, or NULL. */
struct app_id_resolver_manager *app_id_resolver_manager_get(void);

/* Whether a resolver is currently bound and able to answer requests... */
bool app_id_resolver_manager_available(struct app_id_resolver_manager *manager);

/* Resolve @pidfd into an application id. */
bool app_id_resolver_manager_resolve_pidfd(struct app_id_resolver_manager *manager, int pidfd,
    app_id_resolver_cb callback, void *user_data);

#endif  /* APP_ID_RESOLVER_H */
