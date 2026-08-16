

#ifndef OCCULTATION_EXOSKELETON_SWITCHER_ITEM_H_
#define OCCULTATION_EXOSKELETON_SWITCHER_ITEM_H_

#include <QAbstractItemModel>
#include <QObject>
#include <QRect>

namespace Occultation {

class SwitcherItem : public QObject {
  Q_OBJECT
  Q_PROPERTY(QAbstractItemModel *model READ model NOTIFY modelChanged)
  Q_PROPERTY(
      QRect screenGeometry READ screenGeometry NOTIFY screenGeometryChanged)
  Q_PROPERTY(bool visible READ isVisible NOTIFY visibleChanged)
  Q_PROPERTY(bool allDesktops READ isAllDesktops NOTIFY allDesktopsChanged)
  Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY
                 currentIndexChanged)
  Q_PROPERTY(
      bool noModifierGrab READ noModifierGrab NOTIFY noModifierGrabChanged)
  Q_PROPERTY(bool compositing READ compositing NOTIFY compositingChanged)
  Q_PROPERTY(QObject *item READ item WRITE setItem NOTIFY itemChanged)
  Q_CLASSINFO("DefaultProperty", "item")

 public:
  explicit SwitcherItem(QObject *parent = nullptr);

  QAbstractItemModel *model() const;
  QRect screenGeometry() const;
  bool isVisible() const;
  bool isAllDesktops() const;
  int currentIndex() const;
  void setCurrentIndex(int index);
  bool noModifierGrab() const;
  bool compositing() const;
  QObject *item() const;
  void setItem(QObject *item);

 Q_SIGNALS:
  void visibleChanged();
  void currentIndexChanged(int index);
  void modelChanged();
  void allDesktopsChanged();
  void screenGeometryChanged();
  void itemChanged();
  void noModifierGrabChanged();
  void compositingChanged();

 private:
  QObject *m_item = nullptr;
};

}  // namespace Occultation

#endif
