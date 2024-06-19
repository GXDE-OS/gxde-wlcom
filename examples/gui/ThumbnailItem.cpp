// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <QSGImageNode>
#include <QSGTexture>

#include <QOpenGLContext>
#include <QOpenGLTexture>

#include "ThumbnailItem.h"

#include <QGuiApplication>
#include <libdrm/drm_fourcc.h>
#include <qpa/qplatformnativeinterface.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <sys/mman.h>

class ThumbnailItem::Private
{
  public:
    Private(ThumbnailItem *item);
    ~Private();

    void createEglImage(Thumbnail *thumbnail);
    void imageFromMemfd(Thumbnail *thumbnail);

    EGLImage m_image = EGL_NO_IMAGE_KHR;
    QImage image;
    uint32_t format;
    QSize thum_size;
    ThumInfo *mThumInfo = nullptr;
    void *mem_ptr = nullptr;
    Thumbnail::BufferFlags thumFlags = Thumbnail::BufferFlag::Dmabuf;
};

ThumbnailItem::Private::Private(ThumbnailItem *item) {}

ThumbnailItem::Private::~Private() {}

QSGNode *ThumbnailItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QSGTexture *texture = NULL;

    if (pri->thumFlags & Thumbnail::BufferFlag::Dmabuf) {
        if (pri->m_image == EGL_NO_IMAGE_KHR) {
            QImage q_image(200, 200, QImage::Format_ARGB32_Premultiplied);
            q_image.fill(Qt::blue);
            texture = window()->createTextureFromImage(q_image, QQuickWindow::TextureIsOpaque);
        } else {
            QOpenGLContext *context = window()->openglContext();
            if (!context || !context->isValid()) {
                qWarning() << "OpenGL context is not valid.";
                return NULL;
            }

            QOpenGLTexture *m_texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
            bool created = m_texture->create();
            Q_ASSERT(created);

            static auto s_glEGLImageTargetTexture2DOES =
                (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress(
                    "glEGLImageTargetTexture2DOES");
            if (!s_glEGLImageTargetTexture2DOES) {
                qWarning() << "glEGLImageTargetTexture2DOES is not available" << window();
                return NULL;
            }
            m_texture->bind();
            s_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)pri->m_image);

            m_texture->setWrapMode(QOpenGLTexture::ClampToEdge);
            m_texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
            m_texture->release();
            m_texture->setSize(pri->thum_size.width(), pri->thum_size.height());

            int textureId = m_texture->textureId();

            QQuickWindow::CreateTextureOption textureOption =
                pri->format == DRM_FORMAT_ARGB8888 ? QQuickWindow::TextureHasAlphaChannel
                                                   : QQuickWindow::TextureIsOpaque;
            texture = window()->createTextureFromNativeObject(QQuickWindow::NativeObjectTexture,
                                                              &textureId, 0 /*a vulkan thing?*/,
                                                              pri->thum_size, textureOption);
        }
    } else {
        if (pri->image.isNull()) {
            qWarning() << "image.isNull() " << strerror(errno);
            QImage errorImage(200, 200, QImage::Format_ARGB32_Premultiplied);
            errorImage.fill(Qt::red);
            pri->image = errorImage;
        }

        texture = window()->createTextureFromImage(pri->image, QQuickWindow::TextureIsOpaque);
    }

    QSGImageNode *textureNode = static_cast<QSGImageNode *>(oldNode);
    if (!textureNode) {
        textureNode = window()->createImageNode();
        textureNode->setOwnsTexture(true);
    }
    textureNode->setTexture(texture);

    const auto br = boundingRect().toRect();
    QRect rect({ 0, 0 }, texture->textureSize().scaled(br.size(), Qt::KeepAspectRatio));
    rect.moveCenter(br.center());
    textureNode->setRect(rect);

    return textureNode;
}

ThumbnailItem::ThumbnailItem(QQuickItem *parent) : QQuickItem(parent), pri(new Private(this))
{
    setFlag(ItemHasContents, true); // 必须设置，表明这个Item有内容需要绘制

    struct wl_display *display = NULL;
    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    if (!native) {
        return;
    }

    display = reinterpret_cast<wl_display *>(
        native->nativeResourceForIntegration(QByteArrayLiteral("wl_display")));
    if (!display) {
        qWarning("Failed to get Wayland display.");
        return;
    }

    context = new Context(display, Context::Capability::Thumbnail);
    context->start();
    thumbnail = new Thumbnail(this);
    connect(thumbnail, &Thumbnail::bufferUpdate, this, &ThumbnailItem::BufferImportDmabuf);
}

ThumbnailItem::~ThumbnailItem() {}

#define ADD_ATTRIB(name, value)                                                                    \
    do {                                                                                           \
        attribs[num_attribs++] = (name);                                                           \
        attribs[num_attribs++] = (value);                                                          \
        attribs[num_attribs] = EGL_NONE;                                                           \
    } while (0)

void ThumbnailItem::Private::createEglImage(Thumbnail *thumbnail)
{
    EGLDisplay display = EGL_NO_DISPLAY;
    display = static_cast<EGLDisplay>(
        QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("egldisplay"));

    if (display == EGL_NO_DISPLAY) {
        qWarning() << "egl get display failed ";
        return;
    }

    if (m_image) {
        if (thumFlags & Thumbnail::BufferFlag::Reused)
            return;
        static auto eglDestroyImageKHR =
            (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
        eglDestroyImageKHR(display, m_image);
    }

    EGLint attribs[20] = { EGL_NONE };
    int num_attribs = 0;

    ADD_ATTRIB(EGL_WIDTH, thumbnail->size().width());
    ADD_ATTRIB(EGL_HEIGHT, thumbnail->size().height());
    ADD_ATTRIB(EGL_LINUX_DRM_FOURCC_EXT, thumbnail->format());
    ADD_ATTRIB(EGL_DMA_BUF_PLANE0_FD_EXT, thumbnail->fd());
    ADD_ATTRIB(EGL_DMA_BUF_PLANE0_OFFSET_EXT, thumbnail->offset());
    ADD_ATTRIB(EGL_DMA_BUF_PLANE0_PITCH_EXT, thumbnail->stride());
    if (thumbnail->modifier() != DRM_FORMAT_MOD_INVALID) {
        ADD_ATTRIB(EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, thumbnail->modifier() & 0xFFFFFFFF);
        ADD_ATTRIB(EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, thumbnail->modifier() >> 32);
    }
    ADD_ATTRIB(EGL_IMAGE_PRESERVED_KHR, EGL_TRUE);

    format = thumbnail->format();

    static auto eglCreateImageKHR =
        (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    Q_ASSERT(eglCreateImageKHR);
    m_image = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
    if (m_image == EGL_NO_IMAGE_KHR) {
        qWarning() << "invalid eglCreateImageKHR" << glGetError();
    }
}

void ThumbnailItem::Private::imageFromMemfd(Thumbnail *thumbnail)
{
    if (thumFlags & Thumbnail::BufferFlag::Reused)
        return;

    size_t dataSize = thumbnail->size().height() * thumbnail->stride() + thumbnail->offset();
    uint8_t *map =
        static_cast<uint8_t *>(mmap(nullptr, dataSize, PROT_READ, MAP_PRIVATE, thumbnail->fd(), 0));

    if (map == MAP_FAILED) {
        qWarning() << "Failed to mmap the memory: " << strerror(errno);
        return;
    }
    const QImage::Format qtFormat = thumbnail->stride() / thumbnail->size().width() == 3
                                        ? QImage::Format_RGB888
                                        : QImage::Format_ARGB32;

    QImage img(map, thumbnail->size().width(), thumbnail->size().height(), thumbnail->stride(),
               qtFormat);
    image = img.copy();
    munmap(map, dataSize);
}

void ThumbnailItem::BufferImportDmabuf()
{
    Thumbnail *thum = qobject_cast<Thumbnail *>(sender());
    if (thum->flags() & Thumbnail::BufferFlag::Dmabuf)
        pri->createEglImage(thum);
    else
        pri->imageFromMemfd(thum);

    pri->format = thumbnail->format();
    pri->thumFlags = thumbnail->flags();
    pri->thum_size = thumbnail->size();
    update();
}

void ThumbnailItem::setThumInfo(ThumInfo *info)
{
    if (info && pri->mThumInfo != info) {
        context->thumbnail_init(thumbnail, (Thumbnail::Type)info->type(), info->sourceUuid(),
                                info->outputUuid(), info->removeDecorations());
        pri->mThumInfo = info;
        emit thumInfoChanged();
    } else {
        qWarning() << "Failed to set thumbnail info ." << strerror(errno);
    }
}

ThumInfo *ThumbnailItem::thumInfo() const
{
    return pri->mThumInfo;
}
