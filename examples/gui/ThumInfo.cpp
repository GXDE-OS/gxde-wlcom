// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include "ThumInfo.h"

Thumbnail::Type ThumInfo::type() const
{
    return mType;
}

void ThumInfo::setType(Thumbnail::Type typeId)
{
    if (mType != typeId) {
        mType = typeId;
        emit thumInfoChanged();
    }
}

QString ThumInfo::sourceUuid() const
{
    return mSource;
}

void ThumInfo::setSourceUuid(const QString &sourceId)
{
    if (mSource != sourceId) {
        mSource = sourceId;
        emit thumInfoChanged();
    }
}

QString ThumInfo::outputUuid() const
{
    return mOutput;
}

void ThumInfo::setOutputUuid(const QString &outputId)
{
    if (mOutput != outputId) {
        mOutput = outputId;
        emit thumInfoChanged();
    }
}

QString ThumInfo::removeDecorations() const
{
    return without_decoration;
}

void ThumInfo::setRemoveDecorations(const QString &flag)
{
    if (without_decoration != flag) {
        without_decoration = flag;
        emit thumInfoChanged();
    }
}
