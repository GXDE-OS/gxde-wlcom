// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include "context.h"
#include <libkywc.h>

class Output::Private
{
  public:
    Private(Output *output);
    ~Private();
    void setup(kywc_output *output);

    QString uuid;
    QString name;
    QString make;
    QString model;
    QString serial;
    QString description;
    QSize physical;
    QList<Output::Mode> mode_list;
    Mode cur_mode;
    QPoint point;
    QSize size;
    Output::Capabilities capabilities;
    int32_t transform;
    float scale;
    bool enabled, power, primary;
    uint32_t brightness, color_temp;

    kywc_output *k_output;

  private:
    Output *o;
    static void stateHandle(kywc_output *output, uint32_t mask);
    static void destroyHandle(kywc_output *output);
    static struct kywc_output_interface output_impl;
};

Output::Private::Private(Output *output) : o(output) {}

Output::Private::~Private() {}

void Output::Private::stateHandle(kywc_output *output, uint32_t mask)
{
    Output *o_output = (Output *)kywc_output_get_user_data(output);
    Output::Masks masks;
    if (mask & KYWC_OUTPUT_STATE_ENABLED) {
        o_output->pri->enabled = output->enabled;
        masks |= Output::Mask::Enabled;
    }

    if (mask & KYWC_OUTPUT_STATE_MODE) {
        struct kywc_output_mode *mode = output->mode;
        o_output->pri->cur_mode.size = QSize(mode->width, mode->height);
        o_output->pri->cur_mode.refresh = mode->refresh;
        o_output->pri->cur_mode.preferred = mode->preferred;
        masks |= Output::Mask::Mode;
    }

    if (mask & KYWC_OUTPUT_STATE_POSITION) {
        o_output->pri->point = QPoint(output->x, output->y);
        masks |= Output::Mask::Position;
    }

    if (mask & KYWC_OUTPUT_STATE_TRANSFROM) {
        o_output->pri->transform = output->transform;
        masks |= Output::Mask::Transform;
    }

    if (mask & KYWC_OUTPUT_STATE_SCALE) {
        o_output->pri->scale = output->scale;
        masks |= Output::Mask::Scale;
    }

    if (mask & KYWC_OUTPUT_STATE_POWER) {
        o_output->pri->power = output->power;
        masks |= Output::Mask::Power;
    }

    if (mask & KYWC_OUTPUT_STATE_PRIMARY) {
        o_output->pri->primary = output->primary;
        masks |= Output::Mask::Primary;
    }

    if (mask & KYWC_OUTPUT_STATE_BRIGHTNESS) {
        o_output->pri->brightness = output->brightness;
        masks |= Output::Mask::Brightness;
    }

    if (mask & KYWC_OUTPUT_STATE_COLOR_TEMP) {
        o_output->pri->color_temp = output->color_temp;
        masks |= Output::Mask::ColorTemp;
    }

    emit o_output->stateUpdated(masks);
}

void Output::Private::destroyHandle(kywc_output *output)
{
    Output *o_output = (Output *)kywc_output_get_user_data(output);
    emit o_output->deleted();
}

struct kywc_output_interface Output::Private::output_impl {
    stateHandle, destroyHandle,
};

void Output::Private::setup(kywc_output *output)
{
    k_output = output;
    uuid = QString(output->uuid);
    name = QString(output->name);
    make = QString(output->make);
    model = QString(output->model);
    serial = QString(output->serial);
    description = QString(output->description);
    physical = QSize(output->physical_width, output->physical_height);
    point = QPoint(output->x, output->y);
    transform = output->transform;
    scale = output->scale;
    enabled = output->enabled;
    power = output->power;
    primary = output->primary;
    brightness = output->brightness;
    color_temp = output->color_temp;

    if (output->capabilities & KYWC_OUTPUT_CAPABILITY_POWER)
        capabilities |= Output::Capability::Power;
    if (output->capabilities & KYWC_OUTPUT_CAPABILITY_BRIGHTNESS)
        capabilities |= Output::Capability::Brightness;
    if (output->capabilities & KYWC_OUTPUT_CAPABILITY_COLOR_TEMP)
        capabilities |= Output::Capability::ColorTemp;

    struct kywc_output_mode *output_mode, *tmp;
    wl_list_for_each_safe(output_mode, tmp, &output->modes, link) {
        Mode mod;
        mod.size = QSize(output_mode->width, output_mode->height);
        mod.refresh = output_mode->refresh;
        mod.preferred = output_mode->preferred;
        mode_list.append(mod);
    }

    struct kywc_output_mode *mode = output->mode;
    cur_mode.size = QSize(mode->width, mode->height);
    cur_mode.refresh = mode->refresh;
    cur_mode.preferred = mode->preferred;

    kywc_output_set_user_data(output, this->o);
    kywc_output_set_interface(output, &output_impl);
}

Output::Output(QObject *parent) : pri(new Private(this)) {}

Output::~Output() {}

void Output::setup(kywc_output *output)
{
    pri->setup(output);
}

QString Output::name() const
{
    return pri->name;
}

QString Output::uuid() const
{
    return pri->uuid;
}

QString Output::make() const
{
    return pri->make;
}

QString Output::model() const
{
    return pri->model;
}

QString Output::serial() const
{
    return pri->serial;
}

QString Output::description() const
{
    return pri->description;
}

QSize Output::physicalSize() const
{
    return pri->physical;
}

Output::Capabilities Output::capabilities() const
{
    return pri->capabilities;
}

QList<Output::Mode> Output::modes() const
{
    return pri->mode_list;
}

Output::Mode Output::curMode() const
{
    return pri->cur_mode;
}

QPoint Output::point() const
{
    return pri->point;
}

int Output::transform() const
{
    return pri->transform;
}

float Output::scale() const
{
    return pri->scale;
}

bool Output::isEnabled() const
{
    return pri->enabled;
}

bool Output::isPower() const
{
    return pri->power;
}

bool Output::isPrimary() const
{
    return pri->primary;
}

uint32_t Output::brightness() const
{
    return pri->brightness;
}

uint32_t Output::colorTemp() const
{
    return pri->color_temp;
}

QSize Output::size() const
{
    return pri->size;
}