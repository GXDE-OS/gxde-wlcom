

#include "exoskeleton.h"

#include <libkywc.h>

#include <QCursor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QGuiApplication>
#include <QScreen>
#include <QSocketNotifier>

#include "switcher_model/switcher_model.h"

namespace Occultation {

static const kywc_context_interface s_contextInterface = {
    &Exoskeleton::handleContextCreate, &Exoskeleton::handleContextDestroy,
    &Exoskeleton::handleNewOutput,     &Exoskeleton::handleNewToplevel,
    &Exoskeleton::handleNewWorkspace,
};

Exoskeleton *Exoskeleton::instance() {
  static Exoskeleton runtime;
  return &runtime;
}

Exoskeleton::Exoskeleton(QObject *parent)
    : QObject(parent), m_model(new SwitcherModel(this)) {
  connect(m_model, &SwitcherModel::showDesktopRequested, this,
          &Exoskeleton::showDesktopRequested);
  connect(m_model, &SwitcherModel::showDesktopRequested, this,
          &Exoskeleton::requestShowDesktop);
  connect(qGuiApp, &QGuiApplication::screenAdded, this,
          &Exoskeleton::screenGeometryChanged);
  connect(qGuiApp, &QGuiApplication::screenRemoved, this,
          &Exoskeleton::screenGeometryChanged);
}

Exoskeleton::~Exoskeleton() { stop(); }

SwitcherModel *Exoskeleton::model() const { return m_model; }

kywc_context *Exoskeleton::context() const { return m_context; }

bool Exoskeleton::isStarted() const { return m_context != nullptr; }

bool Exoskeleton::isVisible() const { return m_visible; }

int Exoskeleton::currentIndex() const { return m_currentIndex; }

QRect Exoskeleton::screenGeometry() const {
  if (!m_screenGeometry.isNull()) {
    return m_screenGeometry;
  }
  QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }
  return screen ? screen->geometry() : QRect();
}

bool Exoskeleton::allDesktops() const { return m_allDesktops; }

bool Exoskeleton::noModifierGrab() const { return m_noModifierGrab; }

bool Exoskeleton::start(const QString &displayName) {
  if (m_context) {
    return true;
  }

  const QByteArray display = displayName.toUtf8();
  const uint32_t capabilities = KYWC_CONTEXT_CAPABILITY_OUTPUT |
                                KYWC_CONTEXT_CAPABILITY_TOPLEVEL |
                                KYWC_CONTEXT_CAPABILITY_WORKSPACE |
                                KYWC_CONTEXT_CAPABILITY_THUMBNAIL_EXT |
                                KYWC_CONTEXT_CAPABILITY_IDENTIFIER;
  m_context =
      kywc_context_create(display.isEmpty() ? nullptr : display.constData(),
                          capabilities, &s_contextInterface, this);
  if (!m_context) {
    return false;
  }

  m_notifier = new QSocketNotifier(kywc_context_get_fd(m_context),
                                   QSocketNotifier::Read, this);
  connect(m_notifier, &QSocketNotifier::activated, this,
          &Exoskeleton::processWaylandEvents);
  Q_EMIT startedChanged();
  return true;
}

void Exoskeleton::stop() {
  if (!m_context || m_stopping) {
    return;
  }

  m_stopping = true;
  setVisible(false);
  delete m_notifier;
  m_notifier = nullptr;
  kywc_context *context = m_context;
  kywc_context_destroy(context);
  m_context = nullptr;
  m_stopping = false;
  Q_EMIT startedChanged();
}

void Exoskeleton::show(int direction) {
  if (!m_context && !start()) {
    return;
  }
  if (m_visible) {
    if (direction < 0) {
      previous();
    } else {
      next();
    }
    return;
  }

  m_model->setFrozen(true);
  m_model->refreshDesktopEntry();
  const int count = m_model->rowCount();
  setCurrentIndex(direction < 0 ? count - 1 : (count > 1 ? 1 : 0));
  Q_EMIT screenGeometryChanged();
  setVisible(true);
}

void Exoskeleton::accept() {
  if (!m_visible) {
    return;
  }
  m_model->activate(m_currentIndex);
  setVisible(false);
}

void Exoskeleton::cancel() { setVisible(false); }

void Exoskeleton::next() { setCurrentIndex(m_currentIndex + 1); }

void Exoskeleton::previous() { setCurrentIndex(m_currentIndex - 1); }

void Exoskeleton::setCurrentIndex(int index) {
  const int count = m_model->rowCount();
  if (count <= 0) {
    index = 0;
  } else {
    index %= count;
    if (index < 0) {
      index += count;
    }
  }
  if (m_currentIndex == index) {
    return;
  }
  m_currentIndex = index;
  Q_EMIT currentIndexChanged(index);
}

void Exoskeleton::setScreenGeometry(const QRect &geometry) {
  if (m_screenGeometry == geometry) {
    return;
  }
  m_screenGeometry = geometry;
  Q_EMIT screenGeometryChanged();
}

void Exoskeleton::setAllDesktops(bool allDesktops) {
  if (m_allDesktops == allDesktops) {
    return;
  }
  m_allDesktops = allDesktops;
  Q_EMIT allDesktopsChanged();
}

void Exoskeleton::setNoModifierGrab(bool noModifierGrab) {
  if (m_noModifierGrab == noModifierGrab) {
    return;
  }
  m_noModifierGrab = noModifierGrab;
  Q_EMIT noModifierGrabChanged();
}

QString Exoskeleton::protocolUuid(const QUuid &windowId) const {
  return m_model->protocolUuid(windowId);
}

bool Exoskeleton::isDesktopId(const QUuid &windowId) const {
  return m_model->isDesktopId(windowId);
}

QString Exoskeleton::currentOutputUuid() const {
  if (!m_context) {
    return QString();
  }
  struct Candidate {
    QString primary;
    QString enabled;
    QString intersecting;
    qint64 intersectionArea = 0;
    QRect target;
  } candidate;
  candidate.target = screenGeometry();
  kywc_context_for_each_output(
      m_context,
      [](kywc_output *output, void *data) {
        auto *candidate = static_cast<Candidate *>(data);
        if (!output->enabled || !output->uuid) {
          return false;
        }
        if (candidate->enabled.isEmpty()) {
          candidate->enabled = QString::fromUtf8(output->uuid);
        }
        if (output->primary) {
          candidate->primary = QString::fromUtf8(output->uuid);
        }
        const QRect geometry(output->x, output->y, output->width,
                             output->height);
        const QRect intersection = candidate->target.intersected(geometry);
        const qint64 area =
            static_cast<qint64>(intersection.width()) * intersection.height();
        if (area > candidate->intersectionArea) {
          candidate->intersectionArea = area;
          candidate->intersecting = QString::fromUtf8(output->uuid);
        }
        return false;
      },
      &candidate);
  if (!candidate.intersecting.isEmpty()) {
    return candidate.intersecting;
  }
  return candidate.primary.isEmpty() ? candidate.enabled : candidate.primary;
}

QString Exoskeleton::currentWorkspaceUuid() const {
  if (!m_context) {
    return QString();
  }
  QString uuid;
  kywc_context_for_each_workspace(
      m_context,
      [](kywc_workspace *workspace, void *data) {
        auto *uuid = static_cast<QString *>(data);
        if (workspace->activated && workspace->uuid) {
          *uuid = QString::fromUtf8(workspace->uuid);
          return true;
        }
        return false;
      },
      &uuid);
  return uuid;
}

void Exoskeleton::processWaylandEvents() {
  if (!m_context || kywc_context_process(m_context) >= 0) {
    normalizeCurrentIndex();
    return;
  }
  stop();
  Q_EMIT connectionLost();
}

void Exoskeleton::requestShowDesktop() {
  QDBusMessage message = QDBusMessage::createMethodCall(
      QStringLiteral("com.deepin.wm"), QStringLiteral("/com/deepin/wm"),
      QStringLiteral("com.deepin.wm"), QStringLiteral("SetShowDesktop"));
  message << true;
  QDBusConnection::sessionBus().asyncCall(message);
}

void Exoskeleton::handleContextCreate(kywc_context *, void *) {}

void Exoskeleton::handleContextDestroy(kywc_context *, void *) {}

void Exoskeleton::handleNewOutput(kywc_context *, kywc_output *, void *) {}

void Exoskeleton::handleNewToplevel(kywc_context *, kywc_toplevel *toplevel,
                                    void *data) {
  auto *runtime = static_cast<Exoskeleton *>(data);
  runtime->m_model->addToplevel(toplevel);
  runtime->normalizeCurrentIndex();
}

void Exoskeleton::handleNewWorkspace(kywc_context *, kywc_workspace *, void *) {
}

void Exoskeleton::setVisible(bool visible) {
  if (m_visible == visible) {
    return;
  }
  m_visible = visible;
  if (!visible) {
    m_model->setFrozen(false);
  }
  Q_EMIT visibleChanged();
}

void Exoskeleton::normalizeCurrentIndex() {
  const int oldIndex = m_currentIndex;
  const int count = m_model->rowCount();
  if (count <= 0) {
    m_currentIndex = 0;
  } else if (m_currentIndex >= count) {
    m_currentIndex = count - 1;
  }
  if (oldIndex != m_currentIndex) {
    Q_EMIT currentIndexChanged(m_currentIndex);
  }
}

}  // namespace Occultation
