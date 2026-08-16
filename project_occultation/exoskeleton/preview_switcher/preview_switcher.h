

#ifndef OCCULTATION_EXOSKELETON_PREVIEW_SWITCHER_H_
#define OCCULTATION_EXOSKELETON_PREVIEW_SWITCHER_H_

#include <QAbstractItemModel>
#include <QObject>
#include <QRect>

#include "preview_model/preview_model.h"

namespace Occultation {

class PreviewSwitcher : public QObject {
  Q_OBJECT
  Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
  Q_PROPERTY(QRect screenGeometry READ screenGeometry CONSTANT)
  Q_PROPERTY(bool visible READ isVisible CONSTANT)
  Q_PROPERTY(bool allDesktops READ allDesktops CONSTANT)
  Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY
                 currentIndexChanged)
  Q_PROPERTY(bool noModifierGrab READ noModifierGrab CONSTANT)
  Q_PROPERTY(bool compositing READ compositing CONSTANT)
  Q_PROPERTY(QObject *item READ item WRITE setItem NOTIFY itemChanged)
  Q_CLASSINFO("DefaultProperty", "item")

 public:
  explicit PreviewSwitcher(QObject *parent = nullptr);

  QAbstractItemModel *model();
  QRect screenGeometry() const;
  bool isVisible() const;
  bool allDesktops() const;
  int currentIndex() const;
  void setCurrentIndex(int index);
  bool noModifierGrab() const;
  bool compositing() const;
  QObject *item() const;
  void setItem(QObject *item);

 Q_SIGNALS:
  void currentIndexChanged(int index);
  void itemChanged();

 private:
  PreviewModel m_model;
  QObject *m_item = nullptr;
  int m_currentIndex = 1;
};

}  // namespace Occultation

#endif
