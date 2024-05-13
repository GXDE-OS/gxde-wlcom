#ifndef THUMBNAILITEM_H
#define THUMBNAILITEM_H

#include <QQuickItem>
#include <QQuickWindow>

#include "ThumInfo.h"
#include "context.h"

class ThumbnailItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(ThumInfo *thumInfo READ thumInfo WRITE setThumInfo NOTIFY thumInfoChanged)

  public:
    explicit ThumbnailItem(QQuickItem *parent = nullptr);
    ~ThumbnailItem();

    ThumInfo *thumInfo() const;
    void setThumInfo(ThumInfo *info);

  signals:
    void thumInfoChanged();

  protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

  private Q_SLOTS:
    void BufferImportDmabuf();

  private:
    class Private;
    Private *pri;
    Context *context = nullptr;
    Thumbnail *thumbnail = nullptr;
};

#endif // ThumbnailItem_H
