// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef CONTEXT_H
#define CONTEXT_H

#include <QList>
#include <QPoint>
#include <QPointer>
#include <QSize>

typedef struct _kywc_context kywc_context;
typedef struct _kywc_output kywc_output;
typedef struct _kywc_toplevel kywc_toplevel;
typedef struct _kywc_workspace kywc_workspace;
typedef struct _kywc_thumbnail kywc_thumbnail;

class Workspace : public QObject
{
    Q_OBJECT
  public:
    enum class Mask {
        Name = 1 << 0,
        Position = 1 << 1,
        Activated = 1 << 2,
    };
    Q_DECLARE_FLAGS(Masks, Mask);

    explicit Workspace(QObject *parent = nullptr);
    ~Workspace();

    void setup(kywc_workspace *workspace);
    QString name() const;
    QString uuid() const;
    int position() const;
    bool isActivated() const;

    void setActivate();
    void move(int position);
    void remove();

  Q_SIGNALS:
    void stateUpdated(Workspace::Masks mask);
    void deleted();

  private:
    class Private;
    Private *pri;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Workspace::Masks)

class Output : public QObject
{
    Q_OBJECT
  public:
    struct Mode {
        QSize size;
        int32_t refresh; // mHz
        bool preferred;
    };

    enum class Capability {
        Power = 1 << 0,
        Brightness = 1 << 1,
        ColorTemp = 1 << 2,
    };
    Q_DECLARE_FLAGS(Capabilities, Capability);

    enum class Mask {
        Enabled = 1 << 0,
        Mode = 1 << 1,
        Position = 1 << 2,
        Transform = 1 << 3,
        Scale = 1 << 4,
        Power = 1 << 5,
        Primary = 1 << 6,
        Brightness = 1 << 7,
        ColorTemp = 1 << 8,
    };
    Q_DECLARE_FLAGS(Masks, Mask);

    explicit Output(QObject *parent = nullptr);
    ~Output();

    void setup(kywc_output *output);
    QString name() const;
    QString uuid() const;
    QString make() const;
    QString model() const;
    QString serial() const;
    QString description() const;
    QSize physicalSize() const;
    Output::Capabilities capabilities() const;
    QList<Mode> modes() const;
    Output::Mode curMode() const;
    QPoint point() const;
    QSize size() const;
    int transform() const;
    float scale() const;
    bool isEnabled() const;
    bool isPower() const;
    bool isPrimary() const;
    uint32_t brightness() const;
    uint32_t colorTemp() const;

  Q_SIGNALS:
    void stateUpdated(Output::Masks mask);
    void deleted();

  private:
    class Private;
    Private *pri;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Output::Capabilities)
Q_DECLARE_OPERATORS_FOR_FLAGS(Output::Masks)

class Toplevel : public QObject
{
    Q_OBJECT
  public:
    enum class Capability {
        Taskbar = 1 << 0,
        Switcher = 1 << 1,
    };
    Q_DECLARE_FLAGS(Capabilities, Capability);

    enum class Mask {
        AppId = 1 << 0,
        Title = 1 << 1,
        Activated = 1 << 2,
        Minimized = 1 << 3,
        Maximized = 1 << 4,
        Fullscreen = 1 << 5,
        PrimaryOutput = 1 << 6,
        Workspace = 1 << 7,
        Parent = 1 << 8,
        Icon = 1 << 9,
        Position = 1 << 10,
        Size = 1 << 11,
    };
    Q_DECLARE_FLAGS(Masks, Mask)

    explicit Toplevel(QObject *parent = nullptr);
    ~Toplevel();

    void setup(kywc_toplevel *toplevel);
    QString uuid() const;
    QString title() const;
    QString icon() const;
    QString appId() const;
    QPointer<Toplevel> parent() const;
    QString primaryOutput() const;
    QStringList workspaces() const;
    QPoint point() const;
    QSize size() const;
    Toplevel::Capabilities capabilities() const;
    bool isActivated() const;
    bool isMinimized() const;
    bool isMaximized() const;
    bool isFullscreen() const;

    void setMaximized(QString output);
    void setMinimized();
    void unsetMaximized();
    void unsetMinimized();
    void setFullscreen(QString output);
    void unsetFullscreen();
    void setActivate();
    void close();
    void enterWorkspace(QString workspace);
    void leaveWorkspace(QString workspace);
    void moveToWorkspace(QString workspace);
    void moveToOutput(QString output);

  Q_SIGNALS:
    void stateUpdated(Toplevel::Masks mask);
    void deleted();

  private:
    class Private;
    Private *pri;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Toplevel::Capabilities)
Q_DECLARE_OPERATORS_FOR_FLAGS(Toplevel::Masks)

class Thumbnail : public QObject
{
    Q_OBJECT
  public:
    enum class Type{
        Output,
        Toplevel,
        Workspace,
    };
    Q_DECLARE_FLAGS(Types, Type);

    enum BufferFlag {
        Dmabuf = 1 << 0,
        Reused = 1 << 1,
    };
    Q_DECLARE_FLAGS(BufferFlags, BufferFlag);

    explicit Thumbnail(QObject *parent = nullptr);
    ~Thumbnail();

    struct t_thumbnail {
        Types type;
        QString source_uuid;
        QString output_uuid;
    };

    struct Buffer {
        int32_t fd;
        uint32_t format;
        QSize size;
        uint32_t offset;
        uint32_t stride;
        uint64_t modifier;
        BufferFlags flags;
    };

    void setup(kywc_context *ctx, const char* uuid);

    int32_t fd() const;
    uint32_t format() const;
    QSize size() const;
    uint32_t offset() const;
    uint32_t stride() const;
    uint64_t modifier() const;
    Thumbnail::BufferFlags flags() const;

  Q_SIGNALS:
    void bufferUpdate();
    void deleted();

  private:
    class Private;
    Private *pri;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Thumbnail::Types)
Q_DECLARE_OPERATORS_FOR_FLAGS(Thumbnail::BufferFlags)

class Context : public QObject
{
    Q_OBJECT
  public:
    enum class Capability {
        Output = 1 << 0,
        Toplevel = 1 << 1,
        Workspace = 1 << 2,
        Thumbnail = 1 << 3,
    };
    Q_DECLARE_FLAGS(Capabilities, Capability)

    explicit Context(struct wl_display *display, Capabilities caps, QObject *parent = nullptr);
    ~Context();

    void start();

    void addWorkspace(uint32_t position);
    Workspace *findWorkspace(QString uuid);
    Output *findOutput(QString uuid);
    Toplevel *findToplevel(QString uuid);

    void dispatch();
    Thumbnail *thumbnail_init(const char* uuid);
  Q_SIGNALS:
    void aboutToTeardown();

    void created();
    void destroyed();
    void workespaceAdded(Workspace *workspace);
    void outputAdded(Output *output);
    void toplevelAdded(Toplevel *toplevel);

    // void thumbnailBufferUpdate();
    // void thumbnailDeleted();

  private Q_SLOTS:
    void onContextReady();

  private:
    class Private;
    Private *pri;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Context::Capabilities)

#endif // CONTEXT_H
