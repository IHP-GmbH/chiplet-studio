// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * CellComponentMapper.cpp - Implementation
 */

#include "CellComponentMapper.h"
#include "core/Assembly.h"
#include <QDebug>

namespace chiplet {

void CellComponentMapper::build(const Assembly& assembly, const QStringList& cellNames)
{
    clear();

    for (const auto& comp : assembly.components()) {
        QString compId = QString::fromStdString(comp->id());
        QString prefix = compId + "_";

        for (const QString& cellName : cellNames) {
            if (cellName.startsWith(prefix)) {
                m_cellToComponent[cellName] = compId;
                // Store first match as primary cell for this component
                if (m_componentToCell.find(compId) == m_componentToCell.end()) {
                    m_componentToCell[compId] = cellName;
                }
            }
        }
    }

    if (m_cellToComponent.empty() && !cellNames.isEmpty()) {
        qDebug() << "CellComponentMapper: no wrapper cells matched any component ID";
    }
}

QString CellComponentMapper::componentForCell(const QString& cellName) const
{
    auto it = m_cellToComponent.find(cellName);
    return (it != m_cellToComponent.end()) ? it->second : QString();
}

QString CellComponentMapper::primaryCellForComponent(const QString& componentId) const
{
    auto it = m_componentToCell.find(componentId);
    return (it != m_componentToCell.end()) ? it->second : QString();
}

bool CellComponentMapper::hasMapping() const
{
    return !m_cellToComponent.empty();
}

void CellComponentMapper::clear()
{
    m_cellToComponent.clear();
    m_componentToCell.clear();
}

} // namespace chiplet
