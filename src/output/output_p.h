// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _OUTPUT_P_H_
#define _OUTPUT_P_H_

#include <kywc/identifier.h>
#include <kywc/log.h>

#include "output.h"
#include "server.h"

struct output_manager {
    struct server *server;

    struct wl_list outputs;
    struct wl_list output_configs; /* output pending configs */

    struct kywc_output *fallback_output;
    struct kywc_output *primary_output;
    struct kywc_output *pending_primary;

    struct {
        struct wl_signal new_output;
        struct wl_signal new_enabled_output;
        struct wl_signal primary_output;
        struct wl_signal configured;
        struct wl_signal damage;
    } events;

    struct config *config;
    struct config *layout_config;

    struct wl_listener new_output;
    struct wl_listener configured;
    struct wl_listener server_destroy;
    struct wl_listener server_suspend;
    struct wl_listener server_resume;

    char outputs_layout[UUID_SIZE];
    bool has_layout_manager;
    bool damage_enabled;
    bool force_software_gamma;
};

bool output_manager_config_init(struct output_manager *output_manager);

bool output_read_config(struct output *output, struct kywc_output_state *state);

void output_write_config(struct output *output);

bool output_support_brightness(struct output *output);

bool output_set_backlight(uint32_t value);

bool output_get_backlight(struct kywc_output *kywc_output, uint32_t *brightness);

bool output_set_brightness(struct kywc_output *kywc_output, uint32_t brightness);

struct wlr_output_state;
void output_set_gamma_lut(struct wlr_output *wlr_output, size_t gamma_size,
                          struct wlr_output_state *wlr_state, uint32_t color_temp,
                          uint32_t brightness);

void output_set_pending_primary(struct output *output);

void output_add_common_modes(struct output *output);

bool output_manager_layout_init(struct output_manager *output_manager);

void output_manager_get_layout_configs(struct output_manager *output_manager);

bool output_manager_has_actual_outputs(void);

bool xdg_output_manager_v1_create(struct server *server);

void xdg_output_update_scale(float scale);

bool ky_output_manager_create(struct server *server);

#if HAVE_KDE_OUTPUT
bool kde_output_management_create(struct server *server);
#else
static __attribute__((unused)) inline bool kde_output_management_create(struct server *server)
{
    return false;
}
#endif

#if HAVE_WLR_OUTPUT
bool wlr_output_management_create(struct server *server);
#else
static __attribute__((unused)) inline bool wlr_output_management_create(struct server *server)
{
    return false;
}
#endif

#if 1 // HAVE_LAYOUT
bool layout_manager_create(struct server *server);
#else
static __attribute__((unused)) inline bool layout_manager_create(struct server *server)
{
    return false;
}
#endif

#if HAVE_UKUI_OUTPUT
bool ukui_output_management_create(struct server *server);
#else
static __attribute__((unused)) inline bool ukui_output_management_create(struct server *server)
{
    return false;
}
#endif

#endif /* _OUTPUT_P_H_ */
