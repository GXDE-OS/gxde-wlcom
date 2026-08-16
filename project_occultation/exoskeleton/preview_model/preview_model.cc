

#include "preview_model.h"

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QStringList>

namespace Occultation {

static QIcon previewIcon(const QString &name, const QString &letter,
                         const QColor &color) {
  const QIcon themed = QIcon::fromTheme(name);
  if (!themed.isNull()) {
    return themed;
  }

  QPixmap pixmap(128, 128);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  painter.drawRoundedRect(QRectF(8, 8, 112, 112), 25, 25);
  QFont font = painter.font();
  font.setBold(true);
  font.setPixelSize(58);
  painter.setFont(font);
  painter.setPen(Qt::white);
  painter.drawText(pixmap.rect(), Qt::AlignCenter, letter);
  return QIcon(pixmap);
}

PreviewModel::PreviewModel(QObject *parent) : QAbstractListModel(parent) {
  m_entries = {
      {tr("GXDE Terminal"), QStringLiteral("deepin-terminal"),
       QUuid::createUuid(), true},
      {tr("File Manager"), QStringLiteral("dde-file-manager"),
       QUuid::createUuid(), true},
      {tr("Web Browser"), QStringLiteral("firefox"), QUuid::createUuid(), true},
      {tr("Control Center"), QStringLiteral("preferences-system"),
       QUuid::createUuid(), true},
      {tr("Text Editor"), QStringLiteral("deepin-editor"), QUuid::createUuid(),
       true},
      {tr("Music"), QStringLiteral("deepin-music"), QUuid::createUuid(), true},
      {tr("Calculator"), QStringLiteral("deepin-calculator"),
       QUuid::createUuid(), true},
      {tr("Desktop"), QStringLiteral("user-desktop"),
       QUuid(QStringLiteral("{00000000-0000-0000-0000-000000000001}")), false},
  };
}

int PreviewModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : m_entries.size();
}

QVariant PreviewModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
    return QVariant();
  }

  static const QList<QColor> colors = {
      QColor(QStringLiteral("#263238")), QColor(QStringLiteral("#4b8df8")),
      QColor(QStringLiteral("#ef6c35")), QColor(QStringLiteral("#19a974")),
      QColor(QStringLiteral("#7e57c2")), QColor(QStringLiteral("#ec407a")),
      QColor(QStringLiteral("#00838f")), QColor(QStringLiteral("#607d8b")),
  };
  static const QStringList letters = {
      QStringLiteral(">_"), QStringLiteral("F"), QStringLiteral("W"),
      QStringLiteral("C"),  QStringLiteral("E"), QStringLiteral("M"),
      QStringLiteral("+"),  QStringLiteral("D"),
  };

  const Entry &entry = m_entries.at(index.row());
  switch (role) {
    case Qt::DisplayRole:
    case CaptionRole:
      return entry.caption;
    case DesktopNameRole:
      return QString();
    case MinimizedRole:
      return false;
    case WIdRole:
      return entry.windowId;
    case CloseableRole:
      return entry.closeable;
    case IconRole:
      return previewIcon(entry.iconName, letters.at(index.row()),
                         colors.at(index.row()));
    default:
      return QVariant();
  }
}

QHash<int, QByteArray> PreviewModel::roleNames() const {
  return {
      {CaptionRole, QByteArrayLiteral("caption")},
      {DesktopNameRole, QByteArrayLiteral("desktopName")},
      {MinimizedRole, QByteArrayLiteral("minimized")},
      {WIdRole, QByteArrayLiteral("windowId")},
      {CloseableRole, QByteArrayLiteral("closeable")},
      {IconRole, QByteArrayLiteral("icon")},
  };
}

}  // namespace Occultation
