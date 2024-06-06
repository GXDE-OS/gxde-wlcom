// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include "context.h"
#include <libkywc.h>

class Workspace::Private
{
  public:
    Private(Workspace *workspace);
    ~Private();
    void setup(kywc_workspace *workspace);

    QString uuid;
    QString name;
    uint32_t position;
    bool activated;

    kywc_workspace *k_workspace;

  private:
    Workspace *w_space;
    static void stateHandle(kywc_workspace *workspace, uint32_t mask);
    static void destroyHandle(kywc_workspace *workspace);
    static struct kywc_workspace_interface workspace_impl;
};

Workspace::Private::Private(Workspace *workspace) : w_space(workspace) {}

Workspace::Private::~Private() {}

void Workspace::Private::stateHandle(kywc_workspace *workspace, uint32_t mask)
{
    Workspace *w = (Workspace *)kywc_workspace_get_user_data(workspace);
    Workspace::Masks masks;
    if (mask & KYWC_WORKSPACE_STATE_NAME)
        masks |= Workspace::Mask::Name;
    if (mask & KYWC_WORKSPACE_STATE_POSITION)
        masks |= Workspace::Mask::Position;
    if (mask & KYWC_WORKSPACE_STATE_ACTIVATED)
        masks |= Workspace::Mask::Activated;

    w->pri->name = QString(workspace->name);
    w->pri->position = workspace->position;
    w->pri->activated = workspace->activated;

    emit w->stateUpdated(masks);
}

void Workspace::Private::destroyHandle(kywc_workspace *workspace)
{
    Workspace *w = (Workspace *)kywc_workspace_get_user_data(workspace);
    emit w->deleted();
}

struct kywc_workspace_interface Workspace::Private::workspace_impl = {
    stateHandle,
    destroyHandle,
};

void Workspace::Private::setup(kywc_workspace *workspace)
{
    uuid = workspace->uuid;
    name = workspace->name;
    position = workspace->position;
    activated = workspace->activated;
    k_workspace = workspace;
    kywc_workspace_set_user_data(workspace, w_space);
    kywc_workspace_set_interface(workspace, &workspace_impl);
}

Workspace::Workspace(QObject *parent) : pri(new Private(this)) {}

Workspace::~Workspace() {}

void Workspace::setup(kywc_workspace *workspace)
{
    pri->setup(workspace);
}

QString Workspace::name() const
{
    return pri->name;
}

QString Workspace::uuid() const
{
    return pri->uuid;
}

int Workspace::position() const
{
    return pri->position;
}

bool Workspace::isActivated() const
{
    return pri->activated;
}

void Workspace::setActivate()
{
    kywc_workspace_activate(pri->k_workspace);
}

void Workspace::move(int position)
{
    kywc_workspace_set_position(pri->k_workspace, position);
}

void Workspace::remove()
{
    kywc_workspace_remove(pri->k_workspace);
}