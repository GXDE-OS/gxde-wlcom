// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0
#include <QApplication>
#include <QQuickWindow>
#include <QQmlApplicationEngine>

#include "WindowThumbnail.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QQmlApplicationEngine engine;
    const QUrl url(QStringLiteral("qrc:/main.qml"));
    engine.load(url);

    auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) {
        qCritical() << "Could not find QQuickWindow object";
        return -1;
    }

    WindowThumbnail *win = new WindowThumbnail(window);

    return app.exec();
}