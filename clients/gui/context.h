// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef CONTEXT_H
#define CONTEXT_H

#include <QSocketNotifier>

#include <libkywc.h>

class Context : public QObject
{
    Q_OBJECT
  public:
    explicit Context(QObject *parent = nullptr);
    ~Context();

  Q_SIGNALS:
    void aboutToTeardown();

  private Q_SLOTS:
    void onContextReady();

  private:
    QSocketNotifier *notifier = nullptr;
    kywc_context *ctx = nullptr;
};

#endif // CONTEXT_H
