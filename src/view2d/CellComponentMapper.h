// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * CellComponentMapper.h - Maps GDS cell names to component IDs
 *
 * Uses the wrapper cell naming convention from hyp_to_gds:
 * wrapper cells are named "<component_id>_<cell_name>" (e.g., U1_Interposer_ANT_CuPi).
 * This allows bidirectional lookup between GDS cells and assembly components.
 */

#ifndef CHIPLET_VIEW2D_CELLCOMPONENTMAPPER_H
#define CHIPLET_VIEW2D_CELLCOMPONENTMAPPER_H

#include <QString>
#include <QStringList>
#include <unordered_map>

namespace chiplet {

class Assembly;

class CellComponentMapper {
public:
    CellComponentMapper() = default;

    /**
     * Build mapping from GDS cell names to component IDs.
     * For each component in the assembly, finds cells whose name
     * starts with "<component_id>_" and maps them.
     */
    void build(const Assembly& assembly, const QStringList& cellNames);

    /**
     * Look up which component a GDS cell belongs to.
     * @return Component ID, or empty string if no mapping exists.
     */
    QString componentForCell(const QString& cellName) const;

    /**
     * Look up the primary wrapper cell for a component.
     * @return Cell name, or empty string if no mapping exists.
     */
    QString primaryCellForComponent(const QString& componentId) const;

    bool hasMapping() const;
    void clear();

private:
    std::unordered_map<QString, QString> m_cellToComponent;   // cell -> componentId
    std::unordered_map<QString, QString> m_componentToCell;   // componentId -> primary cell
};

} // namespace chiplet

#endif // CHIPLET_VIEW2D_CELLCOMPONENTMAPPER_H
