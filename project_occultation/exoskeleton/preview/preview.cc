

#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSharedPointer>
#include <QTimer>
#include <QtQml>

#include "icon_item/icon_item.h"
#include "preview_switcher/preview_switcher.h"
#include "preview_thumbnail_item/preview_thumbnail_item.h"

static void initializeOccultationPreviewResources() {
  Q_INIT_RESOURCE(resources);
}

static QQuickWindow *previewWindow() {
  for (QWindow *window : QGuiApplication::allWindows()) {
    if (auto *quickWindow = qobject_cast<QQuickWindow *>(window)) {
      if (quickWindow->isVisible()) {
        return quickWindow;
      }
    }
  }
  return nullptr;
}

static bool savePreview(const QString &path, const QImage &switcher) {
  if (switcher.isNull()) {
    qCritical("The switcher preview scene could not be captured");
    return false;
  }

  QImage canvas(QSize(1600, 900), QImage::Format_ARGB32_Premultiplied);
  QPainter painter(&canvas);
  QLinearGradient wallpaper(canvas.rect().topLeft(),
                            canvas.rect().bottomRight());
  wallpaper.setColorAt(0, QColor(QStringLiteral("#10294a")));
  wallpaper.setColorAt(.55, QColor(QStringLiteral("#176b91")));
  wallpaper.setColorAt(1, QColor(QStringLiteral("#512b73")));
  painter.fillRect(canvas.rect(), wallpaper);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(255, 255, 255, 16));
  painter.drawEllipse(QPointF(1300, 180), 360, 360);
  painter.setBrush(QColor(0, 218, 255, 20));
  painter.drawEllipse(QPointF(260, 780), 430, 430);

  const QPoint position((canvas.width() - switcher.width()) / 2,
                        (canvas.height() - switcher.height()) / 2);
  painter.drawImage(position, switcher);
  painter.end();

  const QFileInfo output(path);
  if (!canvas.save(output.absoluteFilePath())) {
    qCritical().noquote() << "Unable to write" << output.absoluteFilePath();
    return false;
  }
  qInfo().noquote() << output.absoluteFilePath();
  return true;
}

static void capturePreview(QGuiApplication *application, const QString &path) {
  QQuickWindow *window = previewWindow();
  if (!window) {
    qCritical("The switcher preview window was not created");
    application->exit(3);
    return;
  }

  const QSharedPointer<QQuickItemGrabResult> grab =
      window->contentItem()->grabToImage();
  if (!grab) {
    qCritical("The switcher preview scene could not be scheduled for capture");
    application->exit(3);
    return;
  }
  QObject::connect(
      grab.data(), &QQuickItemGrabResult::ready, application,
      [application, grab, path]() {
        application->exit(savePreview(path, grab->image()) ? 0 : 3);
      });
}

int main(int argc, char **argv) {
  QGuiApplication application(argc, argv);
  QGuiApplication::setApplicationName(
      QStringLiteral("occultation-switcher-preview"));
  initializeOccultationPreviewResources();

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral(
      "Standalone visual preview for the GXDE KWin switcher QML"));
  parser.addHelpOption();
  QCommandLineOption screenshotOption(
      QStringList{QStringLiteral("s"), QStringLiteral("screenshot")},
      QStringLiteral("Render the preview into an image and exit."),
      QStringLiteral("path"));
  parser.addOption(screenshotOption);
  parser.process(application);

  qmlRegisterType<Occultation::PreviewSwitcher>("org.gxde.occultation", 1, 0,
                                                "Switcher");
  qmlRegisterType<Occultation::PreviewThumbnailItem>("org.gxde.occultation", 1,
                                                     0, "ThumbnailItem");
  qmlRegisterType<Occultation::IconItem>("org.gxde.occultation", 1, 0,
                                         "IconItem");

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

  if (parser.isSet(screenshotOption)) {
    const QString output = parser.value(screenshotOption);
    QTimer::singleShot(500, &application, [&application, output]() {
      capturePreview(&application, output);
    });
  }
  return application.exec();
}
