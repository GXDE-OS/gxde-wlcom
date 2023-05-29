#ifndef _OUTPUT_H_
#define _OUTPUT_H_

#include <kywc/output.h>

struct server;

struct output {
    struct kywc_output base;
    struct wlr_output *wlr_output;

    struct wl_list link;
    struct output_manager *manager;

    // geometry
    // usable_area
    // modes and others

    struct wl_listener frame;
    struct wl_listener damage;
    struct wl_listener needs_frame;
    struct wl_listener destroy;
};

struct output_manager *output_manager_create(struct server *server);

void output_manager_add_configured_listener(struct wl_listener *listener);

void output_manager_emit_configured(void);

struct kywc_output *kywc_output_from_resource(struct wl_resource *resource);



struct output *output_from_kywc_output(struct kywc_output *kywc_output);

#endif /* _OUTPUT_H_ */
