

#ifndef OCCULTATION_EXOSKELETON_ICON_ITEM_H_
#define OCCULTATION_EXOSKELETON_ICON_ITEM_H_

#include <QQuickPaintedItem>
#include <QVariant>

namespace Occultation {

class IconItem : public QQuickPaintedItem {
  Q_OBJECT
  Q_PROPERTY(QVariant icon READ icon WRITE setIcon NOTIFY iconChanged)

 public:
  explicit IconItem(QQuickItem *parent = nullptr);

  QVariant icon() const;
  void setIcon(const QVariant &icon);
  void paint(QPainter *painter) override;

 Q_SIGNALS:
  void iconChanged();

 private:
  QVariant m_icon;
};

}  // namespace Occultation

#endif
