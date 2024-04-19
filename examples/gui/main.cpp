// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0
#include <QApplication>
#include "widget.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    MainWindow w;
    w.setGeometry(300, 130, 700, 500);
    w.show();
    return app.exec();
}

