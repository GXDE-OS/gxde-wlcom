#ifndef THUMINFO_H
#define THUMINFO_H

#include <QObject>

enum Type {
    Output,
    Toplevel,
    Workspace,
};

class ThumInfo : public QObject
{
    Q_OBJECT

  public:
    explicit ThumInfo(QObject *parent = nullptr) {}
    ~ThumInfo() {}

    Type type() const { return mType; }
    void setType(Type typeId)
    {
        if (mType != typeId) {
            mType = typeId;
            emit thumInfoChanged();
        }
    }

    QString sourceUuid() const { return mSource; }
    void setSourceUuid(const QString &sourceId)
    {
        if (mSource != sourceId) {
            mSource = sourceId;
            emit thumInfoChanged();
        }
    }

    QString outputUuid() const { return mOutput; }
    void setOutputUuid(const QString &outputId)
    {
        if (mOutput != outputId) {
            mOutput = outputId;
            emit thumInfoChanged();
        }
    }

  signals:
    void thumInfoChanged();

  private:
    Type mType = Output;
    QString mSource;
    QString mOutput;
};

#endif