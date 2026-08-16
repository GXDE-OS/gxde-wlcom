

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>

#include "registration/registration.h"

int main(int argc, char **argv) {
  QGuiApplication application(argc, argv);
  Occultation::registerQmlTypes();

  QQmlEngine engine;
  QQmlComponent component(
      &engine,
      QUrl(QStringLiteral("qrc:/org/gxde/occultation/switcher/main.qml")));
  if (component.isError()) {
    qCritical().noquote() << component.errorString();
    return 1;
  }

  QScopedPointer<QObject> root(component.create());
  if (!root) {
    qCritical().noquote() << component.errorString();
    return 2;
  }
  return 0;
}
