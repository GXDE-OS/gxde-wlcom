

#include "switcher_item.h"

#include <QAbstractItemModel>

#include "exoskeleton/exoskeleton.h"
#include "switcher_model/switcher_model.h"

namespace Occultation {

SwitcherItem::SwitcherItem(QObject *parent) : QObject(parent) {
  Exoskeleton *runtime = Exoskeleton::instance();
  connect(runtime, &Exoskeleton::visibleChanged, this,
          &SwitcherItem::visibleChanged);
  connect(runtime, &Exoskeleton::currentIndexChanged, this,
          &SwitcherItem::currentIndexChanged);
  connect(runtime, &Exoskeleton::allDesktopsChanged, this,
          &SwitcherItem::allDesktopsChanged);
  connect(runtime, &Exoskeleton::screenGeometryChanged, this,
          &SwitcherItem::screenGeometryChanged);
  connect(runtime, &Exoskeleton::noModifierGrabChanged, this,
          &SwitcherItem::noModifierGrabChanged);
}

QAbstractItemModel *SwitcherItem::model() const {
  return Exoskeleton::instance()->model();
}

QRect SwitcherItem::screenGeometry() const {
  return Exoskeleton::instance()->screenGeometry();
}

bool SwitcherItem::isVisible() const {
  return Exoskeleton::instance()->isVisible();
}

bool SwitcherItem::isAllDesktops() const {
  return Exoskeleton::instance()->allDesktops();
}

int SwitcherItem::currentIndex() const {
  return Exoskeleton::instance()->currentIndex();
}

void SwitcherItem::setCurrentIndex(int index) {
  Exoskeleton::instance()->setCurrentIndex(index);
}

bool SwitcherItem::noModifierGrab() const {
  return Exoskeleton::instance()->noModifierGrab();
}

bool SwitcherItem::compositing() const { return true; }

QObject *SwitcherItem::item() const { return m_item; }

void SwitcherItem::setItem(QObject *item) {
  if (m_item == item) {
    return;
  }
  m_item = item;
  Q_EMIT itemChanged();
}

}  // namespace Occultation
