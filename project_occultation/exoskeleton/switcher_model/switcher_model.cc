

#include "switcher_model.h"

#include <libkywc.h>

#include <QCryptographicHash>
#include <QFileInfo>
#include <QIcon>
#include <QStringList>

#include "exoskeleton/exoskeleton.h"

namespace Occultation {

static const QUuid s_desktopId(
    QStringLiteral("{00000000-0000-0000-0000-000000000001}"));

struct SwitcherModel::Entry {
  SwitcherModel *model = nullptr;
  kywc_toplevel *toplevel = nullptr;
  QString protocolUuid;
  QUuid windowId;
  QString caption;
  QString appId;
  QString iconName;
  QIcon icon;
  bool minimized = false;
  bool activated = false;
  bool desktop = false;
};

static bool isDesktopToplevel(const kywc_toplevel *toplevel) {
  if (!(toplevel->capabilities & KYWC_TOPLEVEL_CAPABILITY_SKIP_SWITCHER) ||
      !(toplevel->capabilities & KYWC_TOPLEVEL_CAPABILITY_SKIP_TASKBAR)) {
    return false;
  }

  const QString appId =
      QString::fromUtf8(toplevel->app_id ? toplevel->app_id : "").toLower();
  static const QStringList desktopAppIds = {
      QStringLiteral("dde-desktop"),
      QStringLiteral("deepin-desktop"),
      QStringLiteral("gxde-desktop"),
      QStringLiteral("org.deepin.desktop"),
      QStringLiteral("org.deepin.dde.desktop"),
      QStringLiteral("org.gxde.desktop"),
  };
  return desktopAppIds.contains(appId);
}

static QUuid stableWindowId(const QString &protocolUuid) {
  QUuid id(protocolUuid);
  if (!id.isNull()) {
    return id;
  }

  QByteArray bytes = QCryptographicHash::hash(protocolUuid.toUtf8(),
                                              QCryptographicHash::Sha256);
  bytes.truncate(16);
  return QUuid::fromRfc4122(bytes);
}

static QIcon resolveIcon(const QString &iconName, const QString &appId) {
  if (!iconName.isEmpty()) {
    if (QFileInfo::exists(iconName)) {
      return QIcon(iconName);
    }
    QIcon icon = QIcon::fromTheme(iconName);
    if (!icon.isNull()) {
      return icon;
    }
  }

  QIcon icon = QIcon::fromTheme(appId);
  if (!icon.isNull()) {
    return icon;
  }
  return QIcon::fromTheme(QStringLiteral("application-x-executable"));
}

SwitcherModel::SwitcherModel(QObject *parent) : QAbstractListModel(parent) {}

SwitcherModel::~SwitcherModel() {
  qDeleteAll(m_entries);
  qDeleteAll(m_desktopEntries);
}

int SwitcherModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return m_entries.size() + 1;
}

QVariant SwitcherModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return QVariant();
  }

  if (index.row() == m_entries.size()) {
    switch (role) {
      case Qt::DisplayRole:
      case CaptionRole:
        return tr("Desktop");
      case DesktopNameRole:
        return QString();
      case IconRole:
        return QIcon::fromTheme(QStringLiteral("user-desktop"));
      case WIdRole: {
        const Entry *desktop = desktopEntry();
        return desktop ? desktop->windowId : s_desktopId;
      }
      case MinimizedRole:
        return false;
      case CloseableRole:
        return false;
      default:
        return QVariant();
    }
  }

  const Entry *entry = m_entries.at(index.row());
  switch (role) {
    case Qt::DisplayRole:
    case CaptionRole:
      return entry->caption.toHtmlEscaped();
    case ClientRole:
      return QVariant::fromValue<void *>(entry->toplevel);
    case DesktopNameRole:
      return QString();
    case IconRole:
      return entry->icon;
    case WIdRole:
      return entry->windowId;
    case MinimizedRole:
      return entry->minimized;
    case CloseableRole:
      return true;
    default:
      return QVariant();
  }
}

QHash<int, QByteArray> SwitcherModel::roleNames() const {
  return {
      {CaptionRole, QByteArrayLiteral("caption")},
      {DesktopNameRole, QByteArrayLiteral("desktopName")},
      {MinimizedRole, QByteArrayLiteral("minimized")},
      {WIdRole, QByteArrayLiteral("windowId")},
      {CloseableRole, QByteArrayLiteral("closeable")},
      {IconRole, QByteArrayLiteral("icon")},
  };
}

QString SwitcherModel::longestCaption() const {
  QString caption;
  for (const Entry *entry : m_entries) {
    if (entry->caption.size() > caption.size()) {
      caption = entry->caption;
    }
  }
  return caption;
}

void SwitcherModel::close(int row) {
  if (row < 0 || row >= m_entries.size()) {
    return;
  }
  kywc_toplevel_close(m_entries.at(row)->toplevel);
}

void SwitcherModel::activate(int row) {
  if (row < 0 || row >= rowCount()) {
    return;
  }
  if (row == m_entries.size()) {
    Q_EMIT showDesktopRequested();
    return;
  }

  Entry *entry = m_entries.at(row);
  if (entry->minimized) {
    kywc_toplevel_unset_minimized(entry->toplevel);
  }
  kywc_toplevel_activate(entry->toplevel);
}

void SwitcherModel::addToplevel(kywc_toplevel *toplevel) {
  if (!toplevel) {
    return;
  }

  const bool desktop = isDesktopToplevel(toplevel);
  if ((toplevel->capabilities & KYWC_TOPLEVEL_CAPABILITY_SKIP_SWITCHER) &&
      !desktop) {
    return;
  }

  auto *entry = new Entry;
  entry->model = this;
  entry->toplevel = toplevel;
  entry->protocolUuid = QString::fromUtf8(toplevel->uuid ? toplevel->uuid : "");
  entry->windowId = stableWindowId(entry->protocolUuid);
  entry->caption = QString::fromUtf8(toplevel->title ? toplevel->title : "");
  entry->appId = QString::fromUtf8(toplevel->app_id ? toplevel->app_id : "");
  entry->iconName = QString::fromUtf8(toplevel->icon ? toplevel->icon : "");
  entry->icon = resolveIcon(entry->iconName, entry->appId);
  entry->minimized = toplevel->minimized;
  entry->activated = toplevel->activated;
  entry->desktop = desktop;

  static const kywc_toplevel_interface interface = {
      &SwitcherModel::handleToplevelState,
      &SwitcherModel::handleToplevelDestroy,
  };
  kywc_toplevel_set_user_data(toplevel, entry);
  kywc_toplevel_set_interface(toplevel, &interface);

  if (desktop) {
    m_desktopEntries.append(entry);
    const int desktopRow = m_entries.size();
    Q_EMIT dataChanged(index(desktopRow, 0), index(desktopRow, 0), {WIdRole});
    return;
  }

  const int row = entry->activated ? 0 : m_entries.size();
  beginInsertRows(QModelIndex(), row, row);
  m_entries.insert(row, entry);
  endInsertRows();
}

void SwitcherModel::removeToplevel(kywc_toplevel *toplevel) {
  for (int i = 0; i < m_desktopEntries.size(); ++i) {
    if (m_desktopEntries.at(i)->toplevel == toplevel) {
      delete m_desktopEntries.takeAt(i);
      const int desktopRow = m_entries.size();
      Q_EMIT dataChanged(index(desktopRow, 0), index(desktopRow, 0), {WIdRole});
      return;
    }
  }

  const int row = rowForToplevel(toplevel);
  if (row < 0) {
    return;
  }

  beginRemoveRows(QModelIndex(), row, row);
  Entry *entry = m_entries.takeAt(row);
  delete entry;
  endRemoveRows();
}

void SwitcherModel::setFrozen(bool frozen) {
  if (m_frozen == frozen) {
    return;
  }
  m_frozen = frozen;
  if (!m_frozen) {
    for (Entry *entry : m_entries) {
      if (entry->activated) {
        promoteActivated(entry);
        break;
      }
    }
  }
}

void SwitcherModel::refreshDesktopEntry() {
  const int row = m_entries.size();
  Q_EMIT dataChanged(index(row, 0), index(row, 0), {WIdRole});
}

QString SwitcherModel::protocolUuid(const QUuid &windowId) const {
  for (const Entry *entry : m_entries) {
    if (entry->windowId == windowId) {
      return entry->protocolUuid;
    }
  }
  for (const Entry *entry : m_desktopEntries) {
    if (entry->windowId == windowId) {
      return entry->protocolUuid;
    }
  }
  return QString();
}

bool SwitcherModel::isDesktopId(const QUuid &windowId) const {
  if (windowId == s_desktopId) {
    return true;
  }
  for (const Entry *entry : m_desktopEntries) {
    if (entry->windowId == windowId) {
      return true;
    }
  }
  return false;
}

void SwitcherModel::handleToplevelState(kywc_toplevel *toplevel,
                                        uint32_t mask) {
  auto *entry = static_cast<Entry *>(kywc_toplevel_get_user_data(toplevel));
  if (entry && entry->model) {
    entry->model->updateEntry(entry, mask);
  }
}

void SwitcherModel::handleToplevelDestroy(kywc_toplevel *toplevel) {
  auto *entry = static_cast<Entry *>(kywc_toplevel_get_user_data(toplevel));
  if (entry && entry->model) {
    entry->model->removeToplevel(toplevel);
  }
}

int SwitcherModel::rowForToplevel(kywc_toplevel *toplevel) const {
  for (int row = 0; row < m_entries.size(); ++row) {
    if (m_entries.at(row)->toplevel == toplevel) {
      return row;
    }
  }
  return -1;
}

int SwitcherModel::rowForEntry(const Entry *entry) const {
  return m_entries.indexOf(const_cast<Entry *>(entry));
}

SwitcherModel::Entry *SwitcherModel::desktopEntry() const {
  if (m_desktopEntries.isEmpty()) {
    return nullptr;
  }
  const QString outputUuid = Exoskeleton::instance()->currentOutputUuid();
  for (Entry *entry : m_desktopEntries) {
    if (entry->toplevel->primary_output &&
        QString::fromUtf8(entry->toplevel->primary_output) == outputUuid) {
      return entry;
    }
  }
  return m_desktopEntries.constFirst();
}

void SwitcherModel::updateEntry(Entry *entry, uint32_t mask) {
  const int row = rowForEntry(entry);
  if (row < 0 && !m_desktopEntries.contains(entry)) {
    return;
  }

  QVector<int> roles;
  if (mask & KYWC_TOPLEVEL_STATE_TITLE) {
    entry->caption =
        QString::fromUtf8(entry->toplevel->title ? entry->toplevel->title : "");
    roles << Qt::DisplayRole << CaptionRole;
  }
  if (mask & KYWC_TOPLEVEL_STATE_APP_ID) {
    entry->appId = QString::fromUtf8(
        entry->toplevel->app_id ? entry->toplevel->app_id : "");
    entry->icon = resolveIcon(entry->iconName, entry->appId);
    roles << IconRole;
  }
  if (mask & KYWC_TOPLEVEL_STATE_ICON) {
    entry->iconName =
        QString::fromUtf8(entry->toplevel->icon ? entry->toplevel->icon : "");
    entry->icon = resolveIcon(entry->iconName, entry->appId);
    roles << IconRole;
  }
  if (mask & KYWC_TOPLEVEL_STATE_MINIMIZED) {
    entry->minimized = entry->toplevel->minimized;
    roles << MinimizedRole;
  }
  if (mask & KYWC_TOPLEVEL_STATE_ACTIVATED) {
    entry->activated = entry->toplevel->activated;
  }

  if (!roles.isEmpty() && row >= 0) {
    Q_EMIT dataChanged(index(row, 0), index(row, 0), roles);
  }
  if ((mask & KYWC_TOPLEVEL_STATE_ACTIVATED) && entry->activated && !m_frozen) {
    promoteActivated(entry);
  }
}

void SwitcherModel::promoteActivated(Entry *entry) {
  const int oldRow = rowForEntry(entry);
  if (oldRow <= 0) {
    return;
  }
  beginMoveRows(QModelIndex(), oldRow, oldRow, QModelIndex(), 0);
  m_entries.move(oldRow, 0);
  endMoveRows();
}

}  // namespace Occultation
