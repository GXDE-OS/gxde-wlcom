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