// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef THUMINFO_H
#define THUMINFO_H

#include <QObject>

#include "context.h"

class ThumInfo : public QObject
{
    Q_OBJECT

  public:
    explicit ThumInfo(QObject *parent = nullptr) {}
    ~ThumInfo() {}

    Thumbnail::Type type() const;
    void setType(Thumbnail::Type typeId);

    QString sourceUuid() const;
    void setSourceUuid(const QString &sourceId);

    QString outputUuid() const;
    void setOutputUuid(const QString &outputId);

    QString removeDecorations() const;
    void setRemoveDecorations(const QString &flag);

  signals:
    void thumInfoChanged();

  private:
    Thumbnail::Type mType = Thumbnail::Type::Output;
    QString mSource;
    QString mOutput;
    QString without_decoration;
};

#endif
