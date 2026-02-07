/**
 * CellSelectionDialog.cpp - Implementation
 */

#include "CellSelectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <cmath>

namespace chiplet {

CellSelectionDialog::CellSelectionDialog(const std::vector<GDSCellInfo>& cells,
                                         const QString& componentName,
                                         const QString& gdsFileName,
                                         bool allowMultiple,
                                         QWidget* parent)
    : QDialog(parent)
    , m_allowMultiple(allowMultiple)
    , m_totalCells(static_cast<int>(cells.size()))
{
    setWindowTitle(tr("Select GDS Cells"));
    setMinimumSize(450, 400);

    setupUI(cells);

    // Set info label text
    QString info = tr("Component: <b>%1</b><br>"
                      "GDS File: <b>%2</b><br>"
                      "Found <b>%3</b> cells")
        .arg(componentName)
        .arg(gdsFileName)
        .arg(cells.size());

    if (allowMultiple) {
        info += tr("<br><i>Multiple selection allowed (flat GDS detected)</i>");
    }

    m_infoLabel->setText(info);
}

void CellSelectionDialog::setupUI(const std::vector<GDSCellInfo>& cells)
{
    auto* mainLayout = new QVBoxLayout(this);

    // Info section
    m_infoLabel = new QLabel(this);
    m_infoLabel->setWordWrap(true);
    mainLayout->addWidget(m_infoLabel);

    // Select all checkbox (only for multiple selection)
    if (m_allowMultiple) {
        m_selectAllCheckbox = new QCheckBox(tr("Select all cells"), this);
        connect(m_selectAllCheckbox, &QCheckBox::stateChanged,
                this, &CellSelectionDialog::onSelectAllChanged);
        mainLayout->addWidget(m_selectAllCheckbox);
    }

    // Cell list in a group box
    auto* listGroup = new QGroupBox(tr("Available Cells"), this);
    auto* listLayout = new QVBoxLayout(listGroup);

    m_cellList = new QListWidget(this);
    m_cellList->setSelectionMode(m_allowMultiple
        ? QAbstractItemView::MultiSelection
        : QAbstractItemView::SingleSelection);

    // Populate list
    for (const auto& cell : cells) {
        auto* item = new QListWidgetItem(m_cellList);
        item->setText(QString::fromStdString(cell.name));
        item->setToolTip(formatCellInfo(cell));

        if (m_allowMultiple) {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            // Pre-select top candidates in flat GDS
            item->setCheckState(cell.isTopCandidate ? Qt::Checked : Qt::Unchecked);
        } else {
            // For hierarchical GDS, select first item (top cell)
            if (m_cellList->count() == 1) {
                item->setSelected(true);
            }
        }

        // Mark top candidates with an icon or prefix
        if (cell.isTopCandidate) {
            item->setText(QString::fromStdString(cell.name) + " [top]");
        }
    }

    connect(m_cellList, &QListWidget::itemChanged,
            this, &CellSelectionDialog::onItemChanged);

    listLayout->addWidget(m_cellList);
    mainLayout->addWidget(listGroup);

    // Dialog buttons
    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(m_buttonBox);

    updateOkButton();
}

QString CellSelectionDialog::formatCellInfo(const GDSCellInfo& cell) const
{
    QString info = tr("Name: %1\n").arg(QString::fromStdString(cell.name));

    if (cell.isTopCandidate) {
        info += tr("Type: Top cell candidate\n");
    } else {
        info += tr("Instances: %1\n").arg(cell.instanceCount);
    }

    info += tr("Shapes: %1\n").arg(cell.shapeCount);

    // Format area in appropriate units
    double area = cell.boundingBoxArea;
    if (area > 1e6) {
        info += tr("Area: %.2f mm2").arg(area / 1e6);
    } else if (area > 1e3) {
        info += tr("Area: %.2f um2").arg(area);
    } else {
        info += tr("Area: %.2f nm2").arg(area * 1e6);
    }

    return info;
}

QStringList CellSelectionDialog::selectedCells() const
{
    QStringList result;

    if (m_allowMultiple) {
        // Return all checked items
        for (int i = 0; i < m_cellList->count(); ++i) {
            QListWidgetItem* item = m_cellList->item(i);
            if (item->checkState() == Qt::Checked) {
                // Remove " [top]" suffix if present
                QString name = item->text();
                if (name.endsWith(" [top]")) {
                    name = name.left(name.length() - 6);
                }
                result.append(name);
            }
        }
    } else {
        // Return selected items
        for (QListWidgetItem* item : m_cellList->selectedItems()) {
            QString name = item->text();
            if (name.endsWith(" [top]")) {
                name = name.left(name.length() - 6);
            }
            result.append(name);
        }
    }

    return result;
}

bool CellSelectionDialog::selectAllChecked() const
{
    return m_selectAllCheckbox && m_selectAllCheckbox->isChecked();
}

void CellSelectionDialog::onSelectAllChanged(int state)
{
    // Block signals to avoid recursive updates
    m_cellList->blockSignals(true);

    Qt::CheckState checkState = (state == Qt::Checked) ? Qt::Checked : Qt::Unchecked;

    for (int i = 0; i < m_cellList->count(); ++i) {
        QListWidgetItem* item = m_cellList->item(i);
        item->setCheckState(checkState);
    }

    m_cellList->blockSignals(false);
    updateOkButton();
}

void CellSelectionDialog::onItemChanged(QListWidgetItem* /*item*/)
{
    // Update "select all" checkbox state
    if (m_selectAllCheckbox) {
        int checkedCount = 0;
        for (int i = 0; i < m_cellList->count(); ++i) {
            if (m_cellList->item(i)->checkState() == Qt::Checked) {
                ++checkedCount;
            }
        }

        m_selectAllCheckbox->blockSignals(true);
        if (checkedCount == 0) {
            m_selectAllCheckbox->setCheckState(Qt::Unchecked);
        } else if (checkedCount == m_totalCells) {
            m_selectAllCheckbox->setCheckState(Qt::Checked);
        } else {
            m_selectAllCheckbox->setCheckState(Qt::PartiallyChecked);
        }
        m_selectAllCheckbox->blockSignals(false);
    }

    updateOkButton();
}

void CellSelectionDialog::updateOkButton()
{
    // Disable OK button if no cells are selected
    bool hasSelection = !selectedCells().isEmpty();
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(hasSelection);
}

} // namespace chiplet
