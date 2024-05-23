#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickView>
#include <QVBoxLayout>
#include <QWidget>

#include "ThumbnailItem.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QStringList arguments = QApplication::arguments();
    QTextStream out(stdout);

    qmlRegisterType<ThumbnailItem>("MyComponents", 1, 0, "ThumbnailItem");

    ThumInfo thum;
    if (arguments.at(1) == "output") {
        thum.setType(Thumbnail::Type::Output);
        thum.setSourceUuid(arguments.at(2));
    } else if (arguments.at(1) == "toplevel") {
        if (arguments.size() != 4) {
            qWarning() << "please input toplevel id and decoration id both";
            return -1;
        }
        thum.setType(Thumbnail::Type::Toplevel);
        thum.setSourceUuid(arguments.at(2));
        thum.setRemoveDecorations(arguments.at(3));
    } else if (arguments.at(1) == "workspace") {
        if (arguments.size() != 4) {
            qWarning() << "please input workspace id and output id both";
            return -1;
        }
        thum.setType(Thumbnail::Type::Workspace);
        thum.setSourceUuid(arguments.at(2));
        thum.setOutputUuid(arguments.at(3));
    } else {
        qWarning() << "please input either output, toplvel or workspace";
        return -1;
    }

    QWidget widget;
    widget.setGeometry(100, 100, 800, 600);
    QVBoxLayout layout(&widget);

    const QUrl url(QStringLiteral("qrc:/main.qml"));
    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.rootContext()->setContextProperty("dataInfo", &thum);
    view.setSource(url);

    QWidget *quickWidget = QWidget::createWindowContainer(&view, &widget);
    layout.addWidget(quickWidget);
    widget.show();

    return app.exec();
}