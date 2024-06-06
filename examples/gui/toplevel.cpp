// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include "context.h"
#include <libkywc.h>

class Toplevel::Private
{
  public:
    Private(Toplevel *toplevel);
    ~Private();
    void setup(kywc_toplevel *toplevel);
    QString uuid;
    QString title;
    QString app_id;
    QString icon;
    QPointer<Toplevel> parent;
    QString primary_output;
    QStringList workspaces;
    QPoint point;
    QSize size;
    Toplevel::Capabilities capabilities;
    bool activated, minimized, maximized, fullscreen;

    kywc_toplevel *k_toplevel;

  private:
    Toplevel *t;
    static void stateHandle(kywc_toplevel *toplevel, uint32_t mask);
    static void destroyHandle(kywc_toplevel *toplevel);
    static struct kywc_toplevel_interface toplevel_impl;
};

Toplevel::Private::Private(Toplevel *toplevel) : t(toplevel) {}

Toplevel::Private::~Private() {}

void Toplevel::Private::stateHandle(kywc_toplevel *toplevel, uint32_t mask)
{
    Toplevel *t_toplevel = (Toplevel *)kywc_toplevel_get_user_data(toplevel);

    Toplevel::Masks t_mask;
    if (mask & KYWC_TOPLEVEL_STATE_APP_ID) {
        t_mask |= Toplevel::Mask::AppId;
        t_toplevel->pri->app_id = QString(toplevel->app_id);
    }

    if (mask & KYWC_TOPLEVEL_STATE_TITLE) {
        t_mask |= Toplevel::Mask::Title;
        t_toplevel->pri->title = QString(toplevel->title);
    }

    if (mask & KYWC_TOPLEVEL_STATE_ACTIVATED) {
        t_mask |= Toplevel::Mask::Activated;
        t_toplevel->pri->activated = toplevel->activated;
    }

    if (mask & KYWC_TOPLEVEL_STATE_MINIMIZED) {
        t_mask |= Toplevel::Mask::Minimized;
        t_toplevel->pri->minimized = toplevel->minimized;
    }

    if (mask & KYWC_TOPLEVEL_STATE_MAXIMIZED) {
        t_mask |= Toplevel::Mask::Maximized;
        t_toplevel->pri->maximized = toplevel->maximized;
    }

    if (mask & KYWC_TOPLEVEL_STATE_FULLSCREEN) {
        t_mask |= Toplevel::Mask::Fullscreen;
        t_toplevel->pri->fullscreen = toplevel->fullscreen;
    }

    if (mask & KYWC_TOPLEVEL_STATE_PRIMARY_OUTPUT) {
        t_mask |= Toplevel::Mask::PrimaryOutput;
        t_toplevel->pri->primary_output = QString(toplevel->primary_output);
    }

    if (mask & KYWC_TOPLEVEL_STATE_WORKSPACE) {
        t_mask |= Toplevel::Mask::Workspace;
        t_toplevel->pri->workspaces.clear();
        for (int i = 0; i < MAX_WORKSPACES; i++) {
            if (toplevel->workspaces[i] == NULL || toplevel->workspaces[i][0] == '\0')
                continue;
            t_toplevel->pri->workspaces << QString(toplevel->workspaces[i]);
        }
    }

    if (mask & KYWC_TOPLEVEL_STATE_PARENT) {
        t_mask |= Toplevel::Mask::Parent;
        if (toplevel->parent) {
            Toplevel *parent_toplevel = new Toplevel;
            parent_toplevel->setup(toplevel->parent);
            t_toplevel->pri->parent = parent_toplevel;
        } else
            t_toplevel->pri->parent = nullptr;
    }

    if (mask & KYWC_TOPLEVEL_STATE_ICON) {
        t_mask |= Toplevel::Mask::Icon;
        t_toplevel->pri->icon = QString(toplevel->icon);
    }

    if (mask & KYWC_TOPLEVEL_STATE_POSITION) {
        t_mask |= Toplevel::Mask::Position;
        t_toplevel->pri->point = QPoint(toplevel->x, toplevel->y);
    }

    if (mask & KYWC_TOPLEVEL_STATE_SIZE) {
        t_mask |= Toplevel::Mask::Size;
        t_toplevel->pri->size = QSize(toplevel->width, toplevel->height);
    }
    emit t_toplevel->stateUpdated(t_mask);
}

void Toplevel::Private::destroyHandle(kywc_toplevel *toplevel)
{
    Toplevel *t_toplevel = (Toplevel *)kywc_toplevel_get_user_data(toplevel);
    emit t_toplevel->deleted();
}

struct kywc_toplevel_interface Toplevel::Private::toplevel_impl {
    stateHandle, destroyHandle,
};

void Toplevel::Private::setup(kywc_toplevel *toplevel)
{
    k_toplevel = toplevel;
    uuid = QString(toplevel->uuid);
    title = QString(toplevel->title);
    icon = QString(toplevel->icon);
    app_id = QString(toplevel->app_id);
    if (toplevel->parent) {
        Toplevel *parent_toplevel = new Toplevel;
        parent_toplevel->setup(toplevel->parent);
        parent = parent_toplevel;
    } else
        parent = nullptr;
    primary_output = toplevel->primary_output;
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        if (toplevel->workspaces[i] == NULL || toplevel->workspaces[i][0] == '\0')
            continue;
        workspaces << QString(toplevel->workspaces[i]);
    }

    if (toplevel->capabilities & KYWC_TOPLEVEL_CAPABILITY_SKIP_TASKBAR)
        capabilities |= Toplevel::Capability::Taskbar;
    if (toplevel->capabilities & KYWC_TOPLEVEL_CAPABILITY_SKIP_SWITCHER)
        capabilities |= Toplevel::Capability::Switcher;

    activated = toplevel->activated;
    minimized = toplevel->minimized;
    maximized = toplevel->maximized;
    fullscreen = toplevel->fullscreen;
    point = QPoint(toplevel->x, toplevel->y);
    size = QSize(toplevel->width, toplevel->height);

    kywc_toplevel_set_interface(toplevel, &toplevel_impl);
    kywc_toplevel_set_user_data(toplevel, t);
}

Toplevel::Toplevel(QObject *parent) : pri(new Private(this)) {}

Toplevel::~Toplevel() {}

void Toplevel::setup(kywc_toplevel *toplevel)
{
    pri->setup(toplevel);
}

QString Toplevel::uuid() const
{
    return pri->uuid;
}

QString Toplevel::title() const
{
    return pri->title;
}

QString Toplevel::icon() const
{
    return pri->icon;
}

QString Toplevel::appId() const
{
    return pri->app_id;
}

QPointer<Toplevel> Toplevel::parent() const
{
    return pri->parent;
}

QString Toplevel::primaryOutput() const
{
    return pri->primary_output;
}

QStringList Toplevel::workspaces() const
{
    return pri->workspaces;
}

QPoint Toplevel::point() const
{
    return pri->point;
}

QSize Toplevel::size() const
{
    return pri->size;
}

Toplevel::Capabilities Toplevel::capabilities() const
{
    return pri->capabilities;
}

bool Toplevel::isActivated() const
{
    return pri->activated;
}

bool Toplevel::isMinimized() const
{
    return pri->minimized;
}

bool Toplevel::isMaximized() const
{
    return pri->maximized;
}

bool Toplevel::isFullscreen() const
{
    return pri->fullscreen;
}

void Toplevel::setMaximized(QString output)
{
    QByteArray qByteArray = output.toUtf8();
    char *str = qByteArray.data();
    kywc_toplevel_set_maximized(pri->k_toplevel, str);
}

void Toplevel::unsetMaximized()
{
    kywc_toplevel_unset_maximized(pri->k_toplevel);
}

void Toplevel::setMinimized()
{
    kywc_toplevel_set_minimized(pri->k_toplevel);
}

void Toplevel::unsetMinimized()
{
    kywc_toplevel_unset_minimized(pri->k_toplevel);
}

void Toplevel::setFullscreen(QString output)
{
    QByteArray qByteArray = output.toUtf8();
    char *str = qByteArray.data();
    kywc_toplevel_set_fullscreen(pri->k_toplevel, str);
}

void Toplevel::unsetFullscreen()
{
    kywc_toplevel_unset_fullscreen(pri->k_toplevel);
}

void Toplevel::setActivate()
{
    kywc_toplevel_activate(pri->k_toplevel);
}

void Toplevel::close()
{
    kywc_toplevel_close(pri->k_toplevel);
}

void Toplevel::enterWorkspace(QString workspace)
{
    QByteArray qByteArray = workspace.toUtf8();
    char *str = qByteArray.data();
    kywc_toplevel_enter_workspace(pri->k_toplevel, str);
}

void Toplevel::leaveWorkspace(QString workspace)
{
    QByteArray qByteArray = workspace.toUtf8();
    char *str = qByteArray.data();
    kywc_toplevel_leave_workspace(pri->k_toplevel, str);
}

void Toplevel::moveToWorkspace(QString workspace)
{
    QByteArray qByteArray = workspace.toUtf8();
    char *str = qByteArray.data();
    kywc_toplevel_move_to_workspace(pri->k_toplevel, str);
}

void Toplevel::moveToOutput(QString output)
{
    QByteArray qByteArray = output.toUtf8();
    char *str = qByteArray.data();
    kywc_toplevel_move_to_output(pri->k_toplevel, str);
}