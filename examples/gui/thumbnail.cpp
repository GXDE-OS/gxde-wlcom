// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include "context.h"
#include <libkywc.h>

class Thumbnail::Private
{
  public:
    Private(Thumbnail *thumbnail);
    ~Private();
    void setup(kywc_context *ctx, Thumbnail::Type type, QString uuid, QString output_uuid,
               QString decoration);

    int32_t fd[4];
    uint32_t format;
    QSize size;
    uint32_t offset[4];
    uint32_t stride[4];
    uint64_t modifier;
    uint32_t planeCount;
    Thumbnail::BufferFlags flags;

    Type type;
    QString source_uuid;
    QString output_uuid;

    kywc_thumbnail *k_thumbnail;

  private:
    Thumbnail *t;
    static bool bufferHandle(kywc_thumbnail *thumbnail, const struct kywc_thumbnail_buffer *buffer,
                             void *data);
    static void destroyHandle(kywc_thumbnail *thumbnail, void *data);
    static struct kywc_thumbnail_interface thumbnail_impl;
};

Thumbnail::Private::Private(Thumbnail *thumbnail) : t(thumbnail) {}

Thumbnail::Private::~Private() {}

bool Thumbnail::Private::bufferHandle(kywc_thumbnail *thumbnail,
                                      const struct kywc_thumbnail_buffer *buffer, void *data)
{
    Thumbnail *thum = (Thumbnail *)data;

    switch (thumbnail->type) {
    case KYWC_THUMBNAIL_TYPE_OUTPUT:
        thum->pri->type = Thumbnail::Type::Output;
        break;
    case KYWC_THUMBNAIL_TYPE_TOPLEVEL:
        thum->pri->type = Thumbnail::Type::Toplevel;
        break;
    case KYWC_THUMBNAIL_TYPE_WORKSPACE:
        thum->pri->type = Thumbnail::Type::Workspace;
        break;
    }

    thum->pri->source_uuid = QString(thumbnail->source_uuid);
    thum->pri->output_uuid = QString(thumbnail->output_uuid);

    if (buffer->flags & KYWC_THUMBNAIL_BUFFER_IS_DMABUF) {
        thum->pri->flags |= Thumbnail::BufferFlag::Dmabuf;
    }

    if (buffer->flags & KYWC_THUMBNAIL_BUFFER_IS_REUSED) {
        thum->pri->flags |= Thumbnail::BufferFlag::Reused;
    }

    thum->pri->size = QSize(buffer->width, buffer->height);

    thum->pri->planeCount = buffer->n_planes;
    for (uint32_t i = 0; i < buffer->n_planes; i++) {
        thum->pri->offset[i] = buffer->planes[i].offset;
        thum->pri->stride[i] = buffer->planes[i].stride;
        thum->pri->fd[i] = buffer->planes[i].fd;
    }

    thum->pri->format = buffer->format;
    thum->pri->modifier = buffer->modifier;

    emit thum->bufferUpdate();
    return true;
}

void Thumbnail::Private::destroyHandle(kywc_thumbnail *thumbnail, void *data)
{
    Thumbnail *thum = (Thumbnail *)data;
    thum->pri->k_thumbnail = nullptr;
    emit thum->deleted();
}

struct kywc_thumbnail_interface Thumbnail::Private::thumbnail_impl {
    bufferHandle, destroyHandle,
};

void Thumbnail::Private::setup(kywc_context *ctx, Thumbnail::Type type, QString uuid,
                               QString output_uuid, QString decoration)
{
    QByteArray qByteArray_uuid = uuid.toUtf8();
    char *str = qByteArray_uuid.data();

    kywc_thumbnail *thumbnail = NULL;

    switch (type) {
    case Thumbnail::Type::Output:
        thumbnail = kywc_thumbnail_create_from_output(ctx, str, &thumbnail_impl, this->t);
        break;
    case Thumbnail::Type::Toplevel: {
        bool without_decoration = false;
        if (decoration == QLatin1String("true")) {
            without_decoration = true;
        }
        thumbnail = kywc_thumbnail_create_from_toplevel(ctx, str, without_decoration,
                                                        &thumbnail_impl, this->t);
    } break;
    case Thumbnail::Type::Workspace: {
        QByteArray qByteArray_uuid = output_uuid.toUtf8();
        char *output = qByteArray_uuid.data();
        thumbnail =
            kywc_thumbnail_create_from_workspace(ctx, str, output, &thumbnail_impl, this->t);
    } break;
    }

    k_thumbnail = thumbnail;
}

Thumbnail::Thumbnail(QObject *parent) : pri(new Private(this)) {}

Thumbnail::~Thumbnail() {}

void Thumbnail::setup(kywc_context *ctx, Thumbnail::Type type, QString uuid, QString output_uuid,
                      QString decoration)
{
    pri->setup(ctx, type, uuid, output_uuid, decoration);
}
void Thumbnail::destory()
{
    if (pri->k_thumbnail) {
        kywc_thumbnail_destroy(pri->k_thumbnail);
        pri->k_thumbnail = nullptr;
    }
}

int32_t Thumbnail::fd() const
{
    return pri->fd[0];
}

int32_t Thumbnail::fd(int index) const
{
    return pri->fd[index];
}

uint32_t Thumbnail::format() const
{
    return pri->format;
}

QSize Thumbnail::size() const
{
    return pri->size;
}

uint32_t Thumbnail::offset() const
{
    return pri->offset[0];
}

uint32_t Thumbnail::offset(int index) const
{
    return pri->offset[index];
}

uint32_t Thumbnail::stride() const
{
    return pri->stride[0];
}

uint32_t Thumbnail::stride(int index) const
{
    return pri->stride[index];
}

uint32_t Thumbnail::planeCount() const
{
    return pri->planeCount;
}

uint64_t Thumbnail::modifier() const
{
    return pri->modifier;
}

Thumbnail::BufferFlags Thumbnail::flags() const
{
    return pri->flags;
}
