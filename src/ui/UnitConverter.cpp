// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * UnitConverter.cpp - Implementation
 */

#include "UnitConverter.h"

namespace chiplet {

UnitConverter& UnitConverter::instance()
{
    static UnitConverter s_instance;
    return s_instance;
}

double UnitConverter::toDisplay(double um) const
{
    switch (m_unit) {
        case DisplayUnit::Nanometers:
            return um * 1000.0;
        case DisplayUnit::Micrometers:
            return um;
        case DisplayUnit::Millimeters:
            return um / 1000.0;
    }
    return um;
}

double UnitConverter::fromDisplay(double value) const
{
    switch (m_unit) {
        case DisplayUnit::Nanometers:
            return value / 1000.0;
        case DisplayUnit::Micrometers:
            return value;
        case DisplayUnit::Millimeters:
            return value * 1000.0;
    }
    return value;
}

QString UnitConverter::toDisplayString(double um, int precision) const
{
    return QString::number(toDisplay(um), 'f', precision) + " " + unitSuffix();
}

QString UnitConverter::unitSuffix() const
{
    switch (m_unit) {
        case DisplayUnit::Nanometers:
            return "nm";
        case DisplayUnit::Micrometers:
            return "um";
        case DisplayUnit::Millimeters:
            return "mm";
    }
    return "um";
}

QStringList UnitConverter::availableUnits()
{
    return QStringList() << "nm" << "um" << "mm";
}

DisplayUnit UnitConverter::unitFromIndex(int index)
{
    switch (index) {
        case 0: return DisplayUnit::Nanometers;
        case 1: return DisplayUnit::Micrometers;
        case 2: return DisplayUnit::Millimeters;
        default: return DisplayUnit::Micrometers;
    }
}

int UnitConverter::indexFromUnit(DisplayUnit unit)
{
    switch (unit) {
        case DisplayUnit::Nanometers: return 0;
        case DisplayUnit::Micrometers: return 1;
        case DisplayUnit::Millimeters: return 2;
    }
    return 1;  // Default to um
}

} // namespace chiplet
