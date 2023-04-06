#ifndef _OUTPUT_H_
#define _OUTPUT_H_

#include <kywc/output.h>

struct server;

struct output {
    struct kywc_output base;

    struct wl_list link;
    struct output_manager *manager;

    // geometry
    // usable_area
    // modes and others

    struct wl_listener frame;
    struct wl_listener destroy;
    const struct output_impl *impl;
    void *data;
};

struct output_impl {
    void (*get_prop)(struct output *output, struct kywc_output_prop *prop);
    void (*get_state)(struct output *output, struct kywc_output_state *state);
    bool (*set_state)(struct output *output, struct kywc_output_state *state);
    void (*frame)(struct output *output);
};

struct output_manager {
    struct wl_list outputs;

    struct kywc_output *primary_output;

    struct {
        struct wl_signal new_output;
        struct wl_signal primary_output;
    } events;

    struct config *config;
    struct wl_listener server_destroy;
};

struct output_manager *output_manager_create(struct server *server);

bool output_manager_config_init(struct output_manager *output_manager);

void output_manager_set_primay_output(struct output *output);

struct output *output_create(const char *name, const struct output_impl *impl, void *data);

void output_destroy(struct output *output);

void output_frame(struct output *output);

bool output_read_config(struct output *output, struct kywc_output_state *state);

void output_write_config(struct output *output);

#endif /* _OUTPUT_H_ */
