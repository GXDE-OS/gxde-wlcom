// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdio.h>

#include <libkywc.h>

static void print_workspace(kywc_workspace *workspace)
{
    printf("workspace \"%s\"\n", workspace->uuid);
    printf("  name: %s\n", workspace->name);
    printf("  position: %d\n", workspace->position);
    printf("  activated: %s\n", workspace->activated ? "true" : "false");
    printf("\n");
}

static void workspace_handle_state(kywc_workspace *workspace, uint32_t mask)
{
    print_workspace(workspace);
}

static void workspace_handle_destroy(kywc_workspace *workspace)
{
    printf("workspace %s is gone\n", workspace->name);
}

static struct kywc_workspace_interface workspace_impl = {
    .state = workspace_handle_state,
    .destroy = workspace_handle_destroy,
};

static void handle_new_workspace(kywc_context *context, kywc_workspace *workspace, void *data)
{
    print_workspace(workspace);
    kywc_workspace_set_interface(workspace, &workspace_impl);
}

static void print_output(kywc_output *output)
{
    printf("output \"%s\"\n", output->uuid);
    printf("  name: %s\n", output->name);
    printf("  enabled: %s\n", output->enabled ? "true" : "false");
    if (output->enabled) {
        printf("  mode: %d x %d @ %d\n", output->mode->width, output->mode->height,
               output->mode->refresh / 1000);
        printf("  position: %d, %d\n", output->x, output->y);
    }
    printf("\n");
}

static void output_handle_state(kywc_output *output, uint32_t mask)
{
    print_output(output);
}

static void output_handle_destroy(kywc_output *output)
{
    printf("output %s is gone\n", output->name);
}

static struct kywc_output_interface output_impl = {
    .state = output_handle_state,
    .destroy = output_handle_destroy,
};

static void handle_new_output(kywc_context *context, kywc_output *output, void *data)
{
    print_output(output);
    kywc_output_set_interface(output, &output_impl);
}

static void print_toplevel(kywc_toplevel *toplevel)
{
    printf("toplevel \"%s\"\n", toplevel->uuid);
    printf("  app_id: %s\n", toplevel->app_id);
    printf("  capabilities: %d\n", toplevel->capabilities);
    printf("  geometry: (%d, %d) %d x %d\n", toplevel->x, toplevel->y, toplevel->width,
           toplevel->height);
    printf("  activated: %s\n", toplevel->activated ? "true" : "false");
    printf("  primary output: %s\n", toplevel->primary_output);
    printf("  workspace:\n");
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        if (toplevel->workspaces[i]) {
            printf("\t%s\n", toplevel->workspaces[i]);
        }
    }
    printf("\n");
}

static void toplevel_handle_state(kywc_toplevel *toplevel, uint32_t mask)
{
    print_toplevel(toplevel);
}

static void toplevel_handle_destroy(kywc_toplevel *toplevel)
{
    printf("toplevel %s is gone\n", toplevel->app_id);
}

static struct kywc_toplevel_interface toplevel_impl = {
    .state = toplevel_handle_state,
    .destroy = toplevel_handle_destroy,
};

static void handle_new_toplevel(kywc_context *context, kywc_toplevel *toplevel, void *data)
{
    print_toplevel(toplevel);
    kywc_toplevel_set_interface(toplevel, &toplevel_impl);
}

static struct kywc_context_interface context_impl = {
    .new_output = handle_new_output,
    .new_toplevel = handle_new_toplevel,
    .new_workspace = handle_new_workspace,
};

int main(int argc, char *argv[])
{
    uint32_t caps = KYWC_CONTEXT_CAPABILITY_WORKSPACE | KYWC_CONTEXT_CAPABILITY_OUTPUT |
                    KYWC_CONTEXT_CAPABILITY_TOPLEVEL;
    kywc_context *ctx = kywc_context_create(NULL, caps, &context_impl, NULL);
    if (!ctx) {
        return -1;
    }

    kywc_context_dispatch(ctx);
    // kywc_context_process(ctx);

    kywc_context_destroy(ctx);

    return 0;
}
