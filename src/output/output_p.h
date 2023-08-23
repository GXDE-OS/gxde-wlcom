#ifndef _OUTPUT_P_H_
#define _OUTPUT_P_H_

#include <kywc/log.h>

#include "output.h"
#include "server.h"

struct output_manager {
    struct server *server;

    struct wl_list outputs;
    struct kywc_output *fallback_output;
    struct kywc_output *primary_output;

    struct {
        struct wl_signal new_output;
        struct wl_signal primary_output;
        struct wl_signal configured;
    } events;

    struct config *config;

    struct wl_listener new_output;
    struct wl_listener server_destroy;
    struct wl_listener server_suspend;
    struct wl_listener server_resume;

    bool has_layout_manager;
};

bool output_manager_config_init(struct output_manager *output_manager);

bool output_read_config(struct output *output, struct kywc_output_state *state);

void output_write_config(struct output *output);

bool output_support_brightness(struct output *output);

bool output_get_brightness(struct kywc_output *kywc_output, int32_t *brightness);

void output_set_brightness(struct kywc_output *kywc_output, int32_t brightness);

void output_set_gamma_lut(struct wlr_output *wlr_output, size_t gamma_size,
                          struct wlr_output_state *wlr_state, uint32_t color_temp);

void output_set_colortemp(struct kywc_output *kywc_output, int32_t color_temp);

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

#endif /* _OUTPUT_P_H_ */
