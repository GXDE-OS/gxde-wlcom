// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include "context.h"
#include <QComboBox>
#include <QDebug>
#include <QTableWidgetItem>

Context::Context(QObject *parent) : QObject{ parent } {}

Context::~Context() {}

static void handle_new_output(kywc_context *context, kywc_output *output, void *data)
{
    Context *ctx = (Context *)data;
    Output *o = new Output;
    o->setup(output);
    emit ctx->outputIsAdded(o);
}

static void handle_new_toplevel(kywc_context *context, kywc_toplevel *toplevel, void *data)
{
    Context *ctx = (Context *)data;
    Toplevel *t = new Toplevel;
    t->setup(toplevel);
    emit ctx->toplevelIsAdded(t);
}

static void handle_new_workspace(kywc_context *context, kywc_workspace *workspace, void *data)
{
    Context *ctx = (Context *)data;
    Workspace *w = new Workspace;
    w->setup(workspace);
    emit ctx->workespaceIsAdded(w);
}

static struct kywc_context_interface context_impl = {
    .new_output = handle_new_output,
    .new_toplevel = handle_new_toplevel,
    .new_workspace = handle_new_workspace,
};

void Context::init(struct wl_display *display, Capabilities caps)
{
    uint32_t capabilities = 0;
    if (caps & Context::Capability::Output)
        capabilities |= KYWC_CONTEXT_CAPABILITY_OUTPUT;
    if (caps & Context::Capability::Toplevel)
        capabilities |= KYWC_CONTEXT_CAPABILITY_TOPLEVEL;
    if (caps & Context::Capability::Workspace)
        capabilities |= KYWC_CONTEXT_CAPABILITY_WORKSPACE;

    if (!display) {
        ctx = kywc_context_create(NULL, capabilities, &context_impl, this);
        if (!ctx) {
            return;
        }

        notifier = new QSocketNotifier(kywc_context_get_fd(ctx), QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, &Context::onContextReady);

        kywc_context_process(ctx);
    } else {
        ctx = kywc_context_create_by_display(display, capabilities, &context_impl, this);
        if (!ctx) {
            return;
        }
    }
}

void Context::clean()
{
    kywc_context_destroy(ctx);
}

void Context::onContextReady()
{
    if (kywc_context_process(ctx) < 0) {
        disconnect(notifier, &QSocketNotifier::activated, this, &Context::onContextReady);
        emit aboutToTeardown();
        exit(-1);
    }
}

void Context::addWorkspace(uint32_t position)
{
    kywc_workspace_create(ctx, NULL, position);
}

Workspace *Context::findWorkspace(QString uuid)
{
    QByteArray qByteArray = uuid.toUtf8();
    char *str = qByteArray.data();
    kywc_workspace *workspace = kywc_context_find_workspace(ctx, str);
    return (Workspace *)kywc_workspace_get_user_data(workspace);
}

Output *Context::findOutput(QString uuid)
{
    QByteArray qByteArray = uuid.toUtf8();
    char *str = qByteArray.data();
    kywc_output *output = kywc_context_find_output(ctx, str);
    return (Output *)kywc_output_get_user_data(output);
}

Toplevel *Context::findToplevel(QString uuid)
{
    QByteArray qByteArray = uuid.toUtf8();
    char *str = qByteArray.data();
    kywc_toplevel *toplevel = kywc_context_find_toplevel(ctx, str);
    return (Toplevel *)kywc_toplevel_get_user_data(toplevel);
}