#ifndef THUMBNAILITEM_H
#define THUMBNAILITEM_H

#include <QAbstractListModel>
#include <QList>
#include <QModelIndex>
#include <QQuickItem>
#include <QQuickWindow>

#include "context.h"
#include "ThumInfo.h"

typedef void *EGLImage;

class ThumbnailItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(ThumInfo *thumInfo READ thumInfo WRITE setThumInfo NOTIFY thumInfoChanged)

  public:
    explicit ThumbnailItem(QQuickItem *parent = nullptr);
    ~ThumbnailItem();

    ThumInfo *thumInfo() const { return mThumInfo; }
    void setThumInfo(ThumInfo *info);

  signals:
    void thumInfoChanged();

  protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

  private:
    void createEglImage(Thumbnail *thumbnail);
    void BufferImportDmabuf();

  private:
    EGLImage m_image;
    uint32_t format;
    Context *context = nullptr;
    Thumbnail *thumbnail = nullptr;
    ThumInfo *mThumInfo = nullptr;
    Thumbnail::BufferFlags bufferIsReused = Thumbnail::BufferFlag::Dmabuf;
};

#endif // ThumbnailItem_H
