#include <libkywc.h>
#include "context.h"

class Thumbnail::Private
{
  public:
    Private(Thumbnail *thumbnail);
    ~Private();
    void setup(kywc_context *ctx, const char* uuid);

    int32_t fd;
    uint32_t format;
    QSize size;
    uint32_t offset;
    uint32_t stride;
    uint64_t modifier;
    Thumbnail::BufferFlags flags;

    Types type;
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

Thumbnail::Private::Private(Thumbnail *thumbnail) : t(thumbnail) {

}

Thumbnail::Private::~Private() {}


bool Thumbnail::Private::bufferHandle(kywc_thumbnail *thumbnail, const struct kywc_thumbnail_buffer *buffer,
                   void *data)
{

    printf("  %s: %d reused: %s\n",
           buffer->flags & KYWC_THUMBNAIL_BUFFER_IS_DMABUF ? "dmabuf" : "memfd", buffer->fd,
           buffer->flags & KYWC_THUMBNAIL_BUFFER_IS_REUSED ? "yes" : "no");
    printf("  format: 0x%x\n", buffer->format);
    printf("  size: %d x %d\n", buffer->width, buffer->height);
    printf("  offset: %d  stride:  %d\n", buffer->offset, buffer->stride);
    printf("  modifier: 0x%lx\n", buffer->modifier);
    printf("\n");

    Thumbnail *thum = (Thumbnail *)data;

    if (thumbnail->type == KYWC_THUMBNAIL_TYPE_OUTPUT) {
        thum->pri->type = Thumbnail::Type::Output;
    } else if (thumbnail->type == KYWC_THUMBNAIL_TYPE_TOPLEVEL) {
        thum->pri->type = Thumbnail::Type::Toplevel;
    } else if (thumbnail->type == KYWC_THUMBNAIL_TYPE_WORKSPACE) {
        thum->pri->type = Thumbnail::Type::Workspace;
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
    thum->pri->offset = buffer->offset;
    thum->pri->stride = buffer->stride;
    thum->pri->fd = buffer->fd;

    thum->pri->format = buffer->format;
    thum->pri->modifier = buffer->modifier;
    // thum->pri->format = QString("%x").arg(buffer->format);
    // thum->pri->modifier = QString("%lx").arg(buffer->modifier);

    emit thum->bufferUpdate();
}

void Thumbnail::Private::destroyHandle(kywc_thumbnail *thumbnail, void *data)
{
    Thumbnail *thum = (Thumbnail *)data;
    emit thum->deleted();
}

struct kywc_thumbnail_interface Thumbnail::Private::thumbnail_impl {
    bufferHandle, destroyHandle,
};

void Thumbnail::Private::setup(kywc_context *ctx, const char* uuid)
{
    kywc_thumbnail *thumbnail = kywc_thumbnail_create(ctx, KYWC_THUMBNAIL_TYPE_TOPLEVEL, uuid, NULL,
                                &thumbnail_impl, this->t);
    k_thumbnail = thumbnail;
    // kywc_thumbnail_set_user_data(thumbnail, t);
}

Thumbnail::Thumbnail(QObject *parent) : pri(new Private(this)) {}

Thumbnail::~Thumbnail() {}

void Thumbnail::setup(kywc_context *ctx, const char* uuid)
{
    pri->setup(ctx, uuid);
}

int32_t Thumbnail::fd() const
{
    return pri->fd;
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
    return pri->offset;
}

uint32_t Thumbnail::stride() const
{
    return pri->stride;
}

uint64_t Thumbnail::modifier() const
{
    return pri->modifier;
}

Thumbnail::BufferFlags Thumbnail::flags() const
{
    return pri->flags;
}
