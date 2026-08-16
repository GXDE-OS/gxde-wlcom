

#include "registration.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QResource>
#include <QtQml>

#include "exoskeleton/exoskeleton.h"
#include "icon_item/icon_item.h"
#include "switcher_item/switcher_item.h"
#include "switcher_model/switcher_model.h"
#include "thumbnail_item/thumbnail_item.h"

static void initializeOccultationResources() { Q_INIT_RESOURCE(resources); }

namespace Occultation {

void registerQmlTypes() {
  static bool registered = false;
  if (registered) {
    return;
  }
  registered = true;
  initializeOccultationResources();

  qmlRegisterType<SwitcherItem>("org.gxde.occultation", 1, 0, "Switcher");
  qmlRegisterType<ThumbnailItem>("org.gxde.occultation", 1, 0, "ThumbnailItem");
  qmlRegisterType<IconItem>("org.gxde.occultation", 1, 0, "IconItem");
  qmlRegisterUncreatableType<SwitcherModel>(
      "org.gxde.occultation", 1, 0, "SwitcherModel",
      QStringLiteral("SwitcherModel is owned by Exoskeleton"));
  qmlRegisterSingletonType<Exoskeleton>(
      "org.gxde.occultation", 1, 0, "Runtime",
      [](QQmlEngine *engine, QJSEngine *) -> QObject * {
        Exoskeleton *runtime = Exoskeleton::instance();
        QQmlEngine::setObjectOwnership(runtime, QQmlEngine::CppOwnership);
        Q_UNUSED(engine)
        return runtime;
      });
}

}  // namespace Occultation
