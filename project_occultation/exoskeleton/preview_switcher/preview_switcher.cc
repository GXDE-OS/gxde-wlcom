

#include "preview_switcher.h"

namespace Occultation {

PreviewSwitcher::PreviewSwitcher(QObject *parent)
    : QObject(parent), m_model(this) {}

QAbstractItemModel *PreviewSwitcher::model() { return &m_model; }

QRect PreviewSwitcher::screenGeometry() const {
  return QRect(0, 0, 1920, 1080);
}

bool PreviewSwitcher::isVisible() const { return true; }

bool PreviewSwitcher::allDesktops() const { return false; }

int PreviewSwitcher::currentIndex() const { return m_currentIndex; }

void PreviewSwitcher::setCurrentIndex(int index) {
  if (m_currentIndex == index) {
    return;
  }
  m_currentIndex = index;
  Q_EMIT currentIndexChanged(index);
}

bool PreviewSwitcher::noModifierGrab() const { return false; }

bool PreviewSwitcher::compositing() const { return true; }

QObject *PreviewSwitcher::item() const { return m_item; }

void PreviewSwitcher::setItem(QObject *item) {
  if (m_item == item) {
    return;
  }
  m_item = item;
  Q_EMIT itemChanged();
}

}  // namespace Occultation
