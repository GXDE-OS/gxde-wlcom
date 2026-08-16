

#ifndef OCCULTATION_EXOSKELETON_PREVIEW_THUMBNAIL_ITEM_H_
#define OCCULTATION_EXOSKELETON_PREVIEW_THUMBNAIL_ITEM_H_

#include <QQuickPaintedItem>
#include <QUuid>

namespace Occultation {

class PreviewThumbnailItem : public QQuickPaintedItem {
  Q_OBJECT
  Q_PROPERTY(QUuid wId READ wId WRITE setWId NOTIFY wIdChanged)

 public:
  explicit PreviewThumbnailItem(QQuickItem *parent = nullptr);

  QUuid wId() const;
  void setWId(const QUuid &windowId);
  void paint(QPainter *painter) override;

 Q_SIGNALS:
  void wIdChanged();

 private:
  QUuid m_windowId;
};

}  // namespace Occultation

#endif
