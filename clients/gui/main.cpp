// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#if 0

#include <QCoreApplication>

#include "context.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    Context contex;

    return a.exec();
}

#else

#include <QGuiApplication>
#include <qpa/qplatformnativeinterface.h>

#include <libkywc.h>

static void handle_new_workspace(kywc_context *context, kywc_workspace *workspace)
{
    printf("new workspace: %s\n", workspace->name);
}

static struct kywc_context_interface context_impl = {
    .new_output = NULL,
    .new_toplevel = NULL,
    .new_workspace = handle_new_workspace,
};

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    // Accessing Wayland display
    struct wl_display *display = NULL;

    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    if (!native) {
        return -1;
    }
    
    display = reinterpret_cast<wl_display *>(native->nativeResourceForIntegration(QByteArrayLiteral("wl_display")));
    if (!display) {
        qWarning("Failed to get Wayland display.");
        return 1;
    }

    uint32_t caps = KYWC_CONTEXT_CAPABILITY_WORKSPACE;
    kywc_context_create_by_display(display, caps, &context_impl);

    return app.exec();
}

#endif
