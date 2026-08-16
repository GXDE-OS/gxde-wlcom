

#include "thumbnail_item.h"

#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>
#include <libkywc.h>
#include <qpa/qplatformnativeinterface.h>
#include <sys/mman.h>

#include <QGuiApplication>
#include <QMetaObject>
#include <QMutexLocker>
#include <QOpenGLContext>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

#include "exoskeleton/exoskeleton.h"

namespace Occultation {

class ThumbnailNode final : public QSGSimpleTextureNode {
 public:
  ~ThumbnailNode() override { replaceTexture(nullptr, 0); }

  void replaceTexture(QSGTexture *texture, GLuint nativeTexture) {
    if (QSGTexture *oldTexture = QSGSimpleTextureNode::texture()) {
      setOwnsTexture(false);
      QSGSimpleTextureNode::setTexture(nullptr);
      delete oldTexture;
    }
    if (m_nativeTexture != 0) {
      glDeleteTextures(1, &m_nativeTexture);
    }

    m_nativeTexture = nativeTexture;
    QSGSimpleTextureNode::setTexture(texture);
    setOwnsTexture(texture != nullptr);
  }

 private:
  GLuint m_nativeTexture = 0;
};

static const kywc_thumbnail_interface s_thumbnailInterface = {
    &ThumbnailItem::handleBuffer,
    &ThumbnailItem::handleDestroy,
};

static PFNEGLCREATEIMAGEKHRPROC createImageFunction() {
  static const auto function = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
      eglGetProcAddress("eglCreateImageKHR"));
  return function;
}

static PFNEGLDESTROYIMAGEKHRPROC destroyImageFunction() {
  static const auto function = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
      eglGetProcAddress("eglDestroyImageKHR"));
  return function;
}

static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC imageTargetFunction() {
  static const auto function =
      reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
          eglGetProcAddress("glEGLImageTargetTexture2DOES"));
  return function;
}

ThumbnailItem::ThumbnailItem(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  connect(Exoskeleton::instance(), &Exoskeleton::startedChanged, this,
          &ThumbnailItem::connectCapture);

  QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
  if (native) {
    m_eglDisplay = static_cast<EGLDisplay>(
        native->nativeResourceForIntegration(QByteArrayLiteral("egldisplay")));
  }
}

ThumbnailItem::~ThumbnailItem() {
  releaseCapture();
  clearPendingFrame();
}

QSize ThumbnailItem::sourceSize() const { return m_sourceSize; }

void ThumbnailItem::setSourceSize(const QSize &sourceSize) {
  if (m_sourceSize == sourceSize) {
    return;
  }
  m_sourceSize = sourceSize;
  update();
  Q_EMIT sourceSizeChanged();
}

qreal ThumbnailItem::brightness() const { return m_brightness; }

void ThumbnailItem::setBrightness(qreal brightness) {
  if (qFuzzyCompare(m_brightness, brightness)) {
    return;
  }
  m_brightness = brightness;
  Q_EMIT brightnessChanged();
}

qreal ThumbnailItem::saturation() const { return m_saturation; }

void ThumbnailItem::setSaturation(qreal saturation) {
  if (qFuzzyCompare(m_saturation, saturation)) {
    return;
  }
  m_saturation = saturation;
  Q_EMIT saturationChanged();
}

QQuickItem *ThumbnailItem::clipTo() const { return m_clipTo; }

void ThumbnailItem::setClipTo(QQuickItem *clip) {
  if (m_clipTo == clip) {
    return;
  }
  m_clipTo = clip;
  Q_EMIT clipToChanged();
}

QUuid ThumbnailItem::wId() const { return m_windowId; }

void ThumbnailItem::setWId(const QUuid &windowId) {
  if (m_windowId == windowId) {
    return;
  }
  releaseCapture();
  clearPendingFrame();
  m_windowId = windowId;
  connectCapture();
  Q_EMIT wIdChanged();
}

QSGNode *ThumbnailItem::updatePaintNode(QSGNode *oldNode,
                                        UpdatePaintNodeData *) {
  auto *node = static_cast<ThumbnailNode *>(oldNode);
  if (!node) {
    node = new ThumbnailNode;
    node->setFiltering(QSGTexture::Linear);
  }

  PendingFrame frame;
  {
    QMutexLocker locker(&m_frameMutex);
    if (!m_pendingFrame.changed) {
      if (node->texture()) {
        node->setRect(paintedRect(m_contentSize));
      }
      return node;
    }
    frame = m_pendingFrame;
    m_pendingFrame = PendingFrame();
  }

  QSGTexture *texture = nullptr;
  GLuint nativeTexture = 0;
  if (!frame.image.isNull()) {
    texture = window()->createTextureFromImage(
        frame.image, frame.image.hasAlphaChannel()
                         ? QQuickWindow::TextureHasAlphaChannel
                         : QQuickWindow::TextureIsOpaque);
  } else if (frame.eglImage != EGL_NO_IMAGE_KHR && imageTargetFunction()) {
    glGenTextures(1, &nativeTexture);
    glBindTexture(GL_TEXTURE_2D, nativeTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    imageTargetFunction()(GL_TEXTURE_2D,
                          reinterpret_cast<GLeglImageOES>(frame.eglImage));
    glBindTexture(GL_TEXTURE_2D, 0);

    const auto option = frame.format == DRM_FORMAT_ARGB8888
                            ? QQuickWindow::TextureHasAlphaChannel
                            : QQuickWindow::TextureIsOpaque;
    texture = window()->createTextureFromNativeObject(
        QQuickWindow::NativeObjectTexture, &nativeTexture, 0, frame.size,
        option);

    if (!texture) {
      glDeleteTextures(1, &nativeTexture);
      nativeTexture = 0;
    }
  }

  if (frame.eglImage != EGL_NO_IMAGE_KHR && destroyImageFunction() &&
      m_eglDisplay != EGL_NO_DISPLAY) {
    destroyImageFunction()(m_eglDisplay, frame.eglImage);
  }

  if (texture) {
    node->replaceTexture(texture, nativeTexture);
    m_contentSize = frame.size;
    node->setRect(paintedRect(frame.size));
  }
  return node;
}

void ThumbnailItem::releaseResources() { clearPendingFrame(); }

void ThumbnailItem::connectCapture() {
  if (m_thumbnail || m_windowId.isNull()) {
    return;
  }

  Exoskeleton *runtime = Exoskeleton::instance();
  kywc_context *context = runtime->context();
  if (!context) {
    return;
  }

  if (runtime->isDesktopId(m_windowId)) {
    const QByteArray desktopUuid = runtime->protocolUuid(m_windowId).toUtf8();
    if (!desktopUuid.isEmpty()) {
      m_thumbnail = kywc_thumbnail_create_from_toplevel(
          context, desktopUuid.constData(), false, &s_thumbnailInterface, this);
      return;
    }

    const QByteArray workspace = runtime->currentWorkspaceUuid().toUtf8();
    const QByteArray output = runtime->currentOutputUuid().toUtf8();
    if (workspace.isEmpty() || output.isEmpty()) {
      return;
    }
    m_thumbnail = kywc_thumbnail_create_from_workspace(
        context, workspace.constData(), output.constData(),
        &s_thumbnailInterface, this);
  } else {
    const QByteArray uuid = runtime->protocolUuid(m_windowId).toUtf8();
    if (uuid.isEmpty()) {
      return;
    }
    m_thumbnail = kywc_thumbnail_create_from_toplevel(
        context, uuid.constData(), false, &s_thumbnailInterface, this);
  }
}

bool ThumbnailItem::handleBuffer(kywc_thumbnail *,
                                 const kywc_thumbnail_buffer *buffer,
                                 void *data) {
  auto *item = static_cast<ThumbnailItem *>(data);
  const bool imported = item->importBuffer(buffer);
  if (imported) {
    QMetaObject::invokeMethod(item, "update", Qt::QueuedConnection);
  }
  return true;
}

void ThumbnailItem::handleDestroy(kywc_thumbnail *, void *data) {
  auto *item = static_cast<ThumbnailItem *>(data);
  item->m_thumbnail = nullptr;
}

bool ThumbnailItem::importBuffer(const kywc_thumbnail_buffer *buffer) {
  if (!buffer || buffer->width == 0 || buffer->height == 0) {
    return false;
  }
  if (buffer->flags & KYWC_THUMBNAIL_BUFFER_IS_DMABUF) {
    return importDmaBuf(buffer);
  }
  return importMemFd(buffer);
}

bool ThumbnailItem::importDmaBuf(const kywc_thumbnail_buffer *buffer) {
  if (m_eglDisplay == EGL_NO_DISPLAY || !createImageFunction()) {
    return false;
  }

  EGLint attributes[64];
  int count = 0;
  auto append = [&](EGLint name, EGLint value) {
    attributes[count++] = name;
    attributes[count++] = value;
  };

  append(EGL_WIDTH, static_cast<EGLint>(buffer->width));
  append(EGL_HEIGHT, static_cast<EGLint>(buffer->height));
  append(EGL_LINUX_DRM_FOURCC_EXT, static_cast<EGLint>(buffer->format));

  struct PlaneAttributes {
    EGLint fd;
    EGLint offset;
    EGLint pitch;
    EGLint modifierLow;
    EGLint modifierHigh;
  };
  static const PlaneAttributes planeAttributes[] = {
      {EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE0_OFFSET_EXT,
       EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
       EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT},
      {EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT,
       EGL_DMA_BUF_PLANE1_PITCH_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
       EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT},
      {EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT,
       EGL_DMA_BUF_PLANE2_PITCH_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
       EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT},
      {EGL_DMA_BUF_PLANE3_FD_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT,
       EGL_DMA_BUF_PLANE3_PITCH_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT,
       EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT},
  };

  const uint32_t planeCount = qMin<uint32_t>(buffer->n_planes, 4);
  for (uint32_t plane = 0; plane < planeCount; ++plane) {
    append(planeAttributes[plane].fd, buffer->planes[plane].fd);
    append(planeAttributes[plane].offset,
           static_cast<EGLint>(buffer->planes[plane].offset));
    append(planeAttributes[plane].pitch,
           static_cast<EGLint>(buffer->planes[plane].stride));
    if (buffer->modifier != DRM_FORMAT_MOD_INVALID) {
      append(planeAttributes[plane].modifierLow,
             static_cast<EGLint>(buffer->modifier & 0xffffffffu));
      append(planeAttributes[plane].modifierHigh,
             static_cast<EGLint>(buffer->modifier >> 32));
    }
  }
  attributes[count++] = EGL_NONE;

  EGLImageKHR image = createImageFunction()(
      m_eglDisplay, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes);
  if (image == EGL_NO_IMAGE_KHR) {
    return false;
  }

  QMutexLocker locker(&m_frameMutex);
  if (m_pendingFrame.eglImage != EGL_NO_IMAGE_KHR && destroyImageFunction()) {
    destroyImageFunction()(m_eglDisplay, m_pendingFrame.eglImage);
  }
  m_pendingFrame.eglImage = image;
  m_pendingFrame.image = QImage();
  m_pendingFrame.size = QSize(buffer->width, buffer->height);
  m_pendingFrame.format = buffer->format;
  m_pendingFrame.changed = true;
  return true;
}

bool ThumbnailItem::importMemFd(const kywc_thumbnail_buffer *buffer) {
  if (buffer->n_planes == 0 || buffer->planes[0].fd < 0) {
    return false;
  }

  const size_t byteCount = static_cast<size_t>(buffer->offset) +
                           static_cast<size_t>(buffer->stride) * buffer->height;
  void *mapping =
      mmap(nullptr, byteCount, PROT_READ, MAP_PRIVATE, buffer->planes[0].fd, 0);
  if (mapping == MAP_FAILED) {
    return false;
  }

  const auto *pixels = static_cast<const uchar *>(mapping) + buffer->offset;
  QImage::Format format = QImage::Format_Invalid;
  switch (buffer->format) {
    case DRM_FORMAT_ARGB8888:
      format = QImage::Format_ARGB32_Premultiplied;
      break;
    case DRM_FORMAT_XRGB8888:
      format = QImage::Format_RGB32;
      break;
    default:
      break;
  }
  if (format == QImage::Format_Invalid) {
    munmap(mapping, byteCount);
    return false;
  }

  QImage image(pixels, buffer->width, buffer->height, buffer->stride, format);
  image = image.copy();
  munmap(mapping, byteCount);

  QMutexLocker locker(&m_frameMutex);
  if (m_pendingFrame.eglImage != EGL_NO_IMAGE_KHR && destroyImageFunction()) {
    destroyImageFunction()(m_eglDisplay, m_pendingFrame.eglImage);
  }
  m_pendingFrame.eglImage = EGL_NO_IMAGE_KHR;
  m_pendingFrame.image = image;
  m_pendingFrame.size = image.size();
  m_pendingFrame.format = buffer->format;
  m_pendingFrame.changed = true;
  return true;
}

void ThumbnailItem::releaseCapture() {
  if (!m_thumbnail) {
    return;
  }
  kywc_thumbnail *thumbnail = m_thumbnail;
  m_thumbnail = nullptr;
  kywc_thumbnail_destroy(thumbnail);
}

void ThumbnailItem::clearPendingFrame() {
  QMutexLocker locker(&m_frameMutex);
  if (m_pendingFrame.eglImage != EGL_NO_IMAGE_KHR && destroyImageFunction() &&
      m_eglDisplay != EGL_NO_DISPLAY) {
    destroyImageFunction()(m_eglDisplay, m_pendingFrame.eglImage);
  }
  m_pendingFrame = PendingFrame();
}

QRectF ThumbnailItem::paintedRect(const QSize &contentSize) const {
  if (contentSize.isEmpty()) {
    return QRectF();
  }
  const QSizeF scaled =
      QSizeF(contentSize).scaled(boundingRect().size(), Qt::KeepAspectRatio);
  const QPointF topLeft(
      boundingRect().x() + (boundingRect().width() - scaled.width()) / 2.0,
      boundingRect().y() + (boundingRect().height() - scaled.height()) / 2.0);
  return QRectF(topLeft, scaled);
}

}  // namespace Occultation
