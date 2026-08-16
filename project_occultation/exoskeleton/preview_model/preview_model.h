

#ifndef OCCULTATION_EXOSKELETON_PREVIEW_MODEL_H_
#define OCCULTATION_EXOSKELETON_PREVIEW_MODEL_H_

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QUuid>

namespace Occultation {

class PreviewModel final : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role {
    CaptionRole = Qt::UserRole + 1,
    DesktopNameRole,
    MinimizedRole,
    WIdRole,
    CloseableRole,
    IconRole,
  };

  explicit PreviewModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

 private:
  struct Entry {
    QString caption;
    QString iconName;
    QUuid windowId;
    bool closeable = true;
  };

  QList<Entry> m_entries;
};

}  // namespace Occultation

#endif
