// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include "context.h"

static void handle_new_workspace(kywc_context *context, kywc_workspace *workspace)
{
    printf("new workspace: %s\n", workspace->name);
}

static struct kywc_context_interface context_impl = {
    .new_output = NULL,
    .new_toplevel = NULL,
    .new_workspace = handle_new_workspace,
};

Context::Context(QObject *parent)
    : QObject{parent}
{
    uint32_t caps = KYWC_CONTEXT_CAPABILITY_WORKSPACE;
    ctx = kywc_context_create(NULL, caps, &context_impl);
    if (!ctx) {
        return;
    } 

    notifier = new QSocketNotifier(kywc_context_get_fd(ctx), QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, &Context::onContextReady);

    kywc_context_process(ctx);
}

void Context::onContextReady()
{
    if (kywc_context_process(ctx) < 0) {
        disconnect(notifier, &QSocketNotifier::activated, this, &Context::onContextReady);
        emit aboutToTeardown();
        exit(-1);
    }
}

Context::~Context()
{
    kywc_context_destroy(ctx);
}
