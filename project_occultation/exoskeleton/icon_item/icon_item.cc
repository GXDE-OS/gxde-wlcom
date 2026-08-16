

#include "icon_item.h"

#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>

namespace Occultation {

IconItem::IconItem(QQuickItem *parent) : QQuickPaintedItem(parent) {
  setAntialiasing(true);
  setMipmap(true);
}

QVariant IconItem::icon() const { return m_icon; }

void IconItem::setIcon(const QVariant &icon) {
  if (m_icon == icon) {
    return;
  }
  m_icon = icon;
  update();
  Q_EMIT iconChanged();
}

void IconItem::paint(QPainter *painter) {
  QIcon icon;
  if (m_icon.canConvert<QIcon>()) {
    icon = qvariant_cast<QIcon>(m_icon);
  } else if (m_icon.canConvert<QPixmap>()) {
    icon = QIcon(qvariant_cast<QPixmap>(m_icon));
  } else if (m_icon.canConvert<QImage>()) {
    icon = QIcon(QPixmap::fromImage(qvariant_cast<QImage>(m_icon)));
  } else {
    const QString name = m_icon.toString();
    icon = QFileInfo::exists(name) ? QIcon(name) : QIcon::fromTheme(name);
  }

  if (icon.isNull()) {
    icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
  }

  painter->setRenderHint(QPainter::SmoothPixmapTransform, smooth());
  const QSize available = boundingRect().size().toSize();
  const QSize actual = icon.actualSize(available);
  QRect target(QPoint(), actual);
  target.moveCenter(boundingRect().center().toPoint());
  icon.paint(painter, target);
}

}  // namespace Occultation
