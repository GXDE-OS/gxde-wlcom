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
    printf("  activated: %s\n\n", workspace->activated ? "true" : "false");
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

static void handle_new_workspace(kywc_context *context, kywc_workspace *workspace)
{
    print_workspace(workspace);
    kywc_workspace_set_interface(workspace, &workspace_impl);
}

static struct kywc_context_interface context_impl = {
    .new_workspace = handle_new_workspace,
};

int main(int argc, char *argv[])
{
    uint32_t caps = KYWC_CONTEXT_CAPABILITY_WORKSPACE;
    kywc_context *ctx = kywc_context_create(NULL, caps, &context_impl);
    if (!ctx) {
        return -1;
    }

    kywc_context_dispatch(ctx);

    kywc_context_destroy(ctx);

    return 0;
}
