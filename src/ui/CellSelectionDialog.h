// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * CellSelectionDialog.h - Dialog for selecting GDS cells
 *
 * Allows user to select one or more cells from a GDS file
 * when the cells are not specified in the .chiplet file.
 */

#ifndef CHIPLET_UI_CELLSELECTIONDIALOG_H
#define CHIPLET_UI_CELLSELECTIONDIALOG_H

#include <QDialog>
#include <QStringList>
#include <vector>
#include "view3d/GDSAnalyzer.h"

class QListWidget;
class QListWidgetItem;
class QLabel;
class QCheckBox;
class QPushButton;
class QDialogButtonBox;

namespace chiplet {

/**
 * CellSelectionDialog allows the user to select one or more cells
 * from a GDS file when cells are not specified in .chiplet
 */
class CellSelectionDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * Create dialog for cell selection
     * @param cells List of cells found in the GDS file
     * @param componentName Name of the component (for display)
     * @param gdsFileName Name of the GDS file (for display)
     * @param allowMultiple If true, allow selecting multiple cells (flat GDS)
     * @param parent Parent widget
     */
    CellSelectionDialog(const std::vector<GDSCellInfo>& cells,
                        const QString& componentName,
                        const QString& gdsFileName,
                        bool allowMultiple = true,
                        QWidget* parent = nullptr);

    /**
     * Get list of selected cell names
     */
    QStringList selectedCells() const;

    /**
     * Check if user wants to select all cells
     */
    bool selectAllChecked() const;

private slots:
    void onSelectAllChanged(int state);
    void onItemChanged(QListWidgetItem* item);
    void updateOkButton();

private:
    void setupUI(const std::vector<GDSCellInfo>& cells);
    QString formatCellInfo(const GDSCellInfo& cell) const;

    QListWidget* m_cellList = nullptr;
    QLabel* m_infoLabel = nullptr;
    QCheckBox* m_selectAllCheckbox = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
    bool m_allowMultiple = true;
    int m_totalCells = 0;
};

} // namespace chiplet

#endif // CHIPLET_UI_CELLSELECTIONDIALOG_H
