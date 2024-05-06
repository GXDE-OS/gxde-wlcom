

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

QSGNode *ThumbnailItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QSGTexture *texture = NULL;

    if (!m_image) {
        QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::blue);
        texture = window()->createTextureFromImage(image, QQuickWindow::TextureIsOpaque);
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
            (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        if (!s_glEGLImageTargetTexture2DOES) {
            qWarning() << "glEGLImageTargetTexture2DOES is not available" << window();
            return NULL;
        }
        m_texture->bind();
        s_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)m_image);

        m_texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        m_texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
        m_texture->release();
        m_texture->setSize(window()->size().width(), window()->size().height());

        int textureId = m_texture->textureId();

        QQuickWindow::CreateTextureOption textureOption = format == DRM_FORMAT_ARGB8888
                                                              ? QQuickWindow::TextureHasAlphaChannel
                                                              : QQuickWindow::TextureIsOpaque;
        texture = window()->createTextureFromNativeObject(QQuickWindow::NativeObjectTexture,
                                                          &textureId, 0 /*a vulkan thing?*/,
                                                          window()->size(), textureOption);
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

ThumbnailItem::ThumbnailItem(QQuickItem *parent) : QQuickItem(parent)
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

void ThumbnailItem::createEglImage(Thumbnail *thumbnail)
{
    EGLDisplay display = EGL_NO_DISPLAY;
    display = static_cast<EGLDisplay>(
        QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("egldisplay"));

    if (display == EGL_NO_DISPLAY) {
        fprintf(stderr, "egl get display failed\n");
        return;
    }

    if (m_image) {
        if (bufferIsReused & Thumbnail::BufferFlag::Reused)
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
        qWarning() << "invalid image" << glGetError();
    }
}

void ThumbnailItem::BufferImportDmabuf()
{
    Thumbnail *thum = qobject_cast<Thumbnail *>(sender());
    createEglImage(thum);
    format = thumbnail->format();
    bufferIsReused = thumbnail->flags();
    update();
}

void ThumbnailItem::setThumInfo(ThumInfo *info)
{
    if (info && mThumInfo != info) {
        context->thumbnail_init(thumbnail, (Thumbnail::Type)info->type(), info->sourceUuid(),
                                info->outputUuid());
        mThumInfo = info;
        emit thumInfoChanged();
    }
}