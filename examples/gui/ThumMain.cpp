#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickView>
#include <QVBoxLayout>
#include <QWidget>

#include "ThumbnailItem.h"
#include "ThumInfo.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qmlRegisterType<ThumbnailItem>("MyComponents", 1, 0, "ThumbnailItem");

    ThumInfo thum;
    // thum.setType(Type::Toplevel);
    // thum.setSourceUuid("b3f98194-2e2e-40ee-94e3-07611b378a87");
    // thum.setOutputUuid("");

    // thum.setType(Type::Workspace);
    // thum.setSourceUuid("302d4158-9491-4e99-bea8-4572af8ac4b5");
    // thum.setOutputUuid("d4d40b7b-5e2e-396a-eaa2-7b5efea5a3ec");

    thum.setType(Type::Output);
    thum.setSourceUuid("d4d40b7b-5e2e-396a-eaa2-7b5efea5a3ec");
    thum.setOutputUuid("");

    const QUrl url(QStringLiteral("qrc:/main.qml"));
    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(800, 600);

    view.rootContext()->setContextProperty("dataInfo", &thum);
    view.setSource(url);
    view.show();

    // QQmlApplicationEngine engine;
    // engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    // engine.rootContext()->setContextProperty("dataInfo", &thum);

    return app.exec();
}