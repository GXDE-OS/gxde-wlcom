// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include "context.h"
#include <QSocketNotifier>
#include <libkywc.h>

class Context::Private
{
  public:
    Private(Context *context);
    ~Private();
    kywc_context *setup(uint32_t capability);
    // void thumbnail_create(const char* uuid);

    kywc_context *k_context = nullptr;
    QSocketNotifier *notifier = nullptr;
    struct wl_display *display = NULL;
    uint32_t capabilities = 0;

  private:
    Context *ctx;
    static void createHandle(kywc_context *context, void *data);
    static void destroyHandle(kywc_context *context, void *data);
    static void newOutput(kywc_context *context, kywc_output *output, void *data);
    static void newToplevel(kywc_context *context, kywc_toplevel *toplevel, void *data);
    static void newWorkspace(kywc_context *context, kywc_workspace *workspace, void *data);
    static struct kywc_context_interface context_impl;

    static bool bufferHandle(kywc_thumbnail *thumbnail, const struct kywc_thumbnail_buffer *buffer,
                   void *data);
    static void destroyHandle(kywc_thumbnail *thumbnail, void *data);
    static struct kywc_thumbnail_interface thumbnail_impl;
};

Context::Private::Private(Context *context) : ctx(context) {}

Context::Private::~Private() {}

void Context::Private::createHandle(kywc_context *context, void *data)
{
    Context::Private *p = (Context::Private *)data;
    emit p->ctx->created();
}

void Context::Private::destroyHandle(kywc_context *context, void *data)
{
    Context::Private *p = (Context::Private *)data;
    emit p->ctx->destroyed();
}

void Context::Private::newOutput(kywc_context *context, kywc_output *output, void *data)
{
    Context::Private *p = (Context::Private *)data;
    Output *o = new Output(p->ctx);
    o->setup(output);
    emit p->ctx->outputAdded(o);
}

void Context::Private::newToplevel(kywc_context *context, kywc_toplevel *toplevel, void *data)
{
    Context::Private *p = (Context::Private *)data;
    Toplevel *t = new Toplevel(p->ctx);
    t->setup(toplevel);
    emit p->ctx->toplevelAdded(t);
}

void Context::Private::newWorkspace(kywc_context *context, kywc_workspace *workspace, void *data)
{
    Context::Private *p = (Context::Private *)data;
    Workspace *w = new Workspace(p->ctx);
    w->setup(workspace);
    emit p->ctx->workespaceAdded(w);
}

struct kywc_context_interface Context::Private::context_impl = {
    createHandle, destroyHandle, newOutput, newToplevel, newWorkspace,
};

kywc_context *Context::Private::setup(uint32_t capabilities)
{
    if (!display)
        k_context = kywc_context_create(NULL, capabilities, &context_impl, this);
    else
        k_context = kywc_context_create_by_display(display, capabilities, &context_impl, this);
    return k_context;
}

Context::Context( struct wl_display *display, Capabilities caps, QObject *parent)
    : QObject{parent}
    , pri(new Private(this))
{
    pri->display = display;

    uint32_t capabilities = 0;
    if (caps & Context::Capability::Output)
        capabilities |= KYWC_CONTEXT_CAPABILITY_OUTPUT;
    if (caps & Context::Capability::Toplevel)
        capabilities |= KYWC_CONTEXT_CAPABILITY_TOPLEVEL;
    if (caps & Context::Capability::Workspace)
        capabilities |= KYWC_CONTEXT_CAPABILITY_WORKSPACE;
    if (caps & Context::Capability::Thumbnail)
        capabilities |= KYWC_CONTEXT_CAPABILITY_THUMBNAIL;
    pri->capabilities = capabilities;
}

Context::~Context() {
    kywc_context_destroy(pri->k_context);
}

void Context::start()
{
    kywc_context *ctx = NULL;

    if (!pri->display) {
        ctx = pri->setup(pri->capabilities);
        if (!ctx) {
            return;
        }
        pri->notifier = new QSocketNotifier(kywc_context_get_fd(ctx), QSocketNotifier::Read, this);
        connect(pri->notifier, &QSocketNotifier::activated, this, &Context::onContextReady);

        kywc_context_process(ctx);
    } else {
        ctx = pri->setup(pri->capabilities);
        if (!ctx) {
            return;
        }
    }
}

void Context::onContextReady()
{
    if (kywc_context_process(pri->k_context) < 0) {
        disconnect(pri->notifier, &QSocketNotifier::activated, this, &Context::onContextReady);
        emit aboutToTeardown();
        exit(-1);
    }
}

void Context::dispatch()
{
    kywc_context_dispatch(pri->k_context);
}

void Context::addWorkspace(uint32_t position)
{
    kywc_workspace_create(pri->k_context, NULL, position);
}

Workspace *Context::findWorkspace(QString uuid)
{
    QByteArray qByteArray = uuid.toUtf8();
    char *str = qByteArray.data();
    kywc_workspace *workspace = kywc_context_find_workspace(pri->k_context, str);
    return (Workspace *)kywc_workspace_get_user_data(workspace);
}

Output *Context::findOutput(QString uuid)
{
    QByteArray qByteArray = uuid.toUtf8();
    char *str = qByteArray.data();
    kywc_output *output = kywc_context_find_output(pri->k_context, str);
    return (Output *)kywc_output_get_user_data(output);
}

Toplevel *Context::findToplevel(QString uuid)
{
    QByteArray qByteArray = uuid.toUtf8();
    char *str = qByteArray.data();
    kywc_toplevel *toplevel = kywc_context_find_toplevel(pri->k_context, str);
    return (Toplevel *)kywc_toplevel_get_user_data(toplevel);
}

Thumbnail *Context::thumbnail_init(const char *uuid)
{
    kywc_context *ctx = NULL;
    pri->display = NULL;

    ctx = pri->setup(pri->capabilities);
    //k_context = kywc_context_create(NULL, capabilities, &context_impl, this);
   // kywc_context *ctx = kywc_context_create(NULL, pri->capabilities, &context_impl, NULL);
    if (!ctx) {
        return NULL;
    }

    // QByteArray qByteArray = uuid.toUtf8();
    // char *str = qByteArray.data();

    kywc_toplevel *toplevel = kywc_context_find_toplevel(ctx, uuid);
    Thumbnail *thum = NULL;
    if (toplevel) {
        thum = new Thumbnail(this);
        thum->setup(ctx, toplevel->uuid);
        //printf("start get thumbnail for toplevel %s\n", toplevel->app_id);
        // kywc_thumbnail *thumbnail = pri->thumbnail_create(toplevel->uuid);
        // kywc_thumbnail_set_user_data(thumbnail, this);
    }

    kywc_context_dispatch(ctx);
    //kywc_context_destroy(ctx);
    return thum;
}