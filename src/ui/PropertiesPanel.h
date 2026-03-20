/**
 * PropertiesPanel.h - Property display for selected component
 *
 * Displays comprehensive read-only properties of selected chiplet component
 * with collapsible groups, layer list from .lyp, and unit selection.
 */

#ifndef CHIPLET_UI_PROPERTIESPANEL_H
#define CHIPLET_UI_PROPERTIESPANEL_H

#include <QWidget>
#include <memory>
#include "core/Component.h"
#include "core/ComponentID.h"
#include "view2d/LayerProperties.h"

class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;
class QGroupBox;
class QLabel;
class QComboBox;
class QTreeWidget;
class QFormLayout;

namespace chiplet {

class Assembly;
class KLayoutBridge;

/**
 * PropertiesPanel displays read-only properties of selected component.
 *
 * Features:
 * - Collapsible property groups (Component, Position, Dimensions, Layout, Layers, Metadata)
 * - Layer list with color swatches from .lyp file
 * - User-selectable unit display (nm/um/mm)
 * - Array configuration display for DieArray components
 */
class PropertiesPanel : public QWidget {
    Q_OBJECT

public:
    explicit PropertiesPanel(QWidget* parent = nullptr);
    ~PropertiesPanel() override;

public slots:
    /**
     * Set the component to display properties for
     * @param componentId ComponentID to display (empty clears selection)
     * @param assembly Assembly to look up component from
     */
    void setComponent(const ComponentID& componentId, Assembly* assembly);

    /**
     * Set the assembly for technology resolution
     * @param assembly Assembly pointer for resolving technology references
     */
    void setAssembly(Assembly* assembly);

    /**
     * Clear selection and reset all fields
     */
    void clearSelection();

signals:
    /**
     * Emitted when component is modified (future use for editing)
     */
    void componentModified(const QString& componentId);

private slots:
    void onUnitChanged(int index);
    void onGroupToggled(bool checked);

private:
    void setupUI();
    void createUnitSelector();
    void createGroupBoxes();
    void updateDisplay();
    void updateComponentGroup();
    void updatePositionGroup();
    void updateDimensionsGroup();
    void updateLayoutGroup();
    void updateLayersGroup();
    void updateArrayGroup();
    void updateMetadataGroup();

    // Helpers
    QString formatValue(double um) const;
    QString formatDegrees(double degrees) const;
    void addPropertyRow(QFormLayout* layout, const QString& label, QLabel*& valueLabel);
    QLabel* createValueLabel(const QString& text = "-");
    QGroupBox* createCollapsibleGroup(const QString& title);
    void loadLayerProperties();

    // Type conversion
    QString typeToString(ComponentType type) const;

    // Data
    ComponentID m_selectedComponentId;
    Assembly* m_assembly = nullptr;
    LayerPropertiesFile m_layerProps;

    // UI Elements
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;
    QVBoxLayout* m_mainLayout = nullptr;
    QComboBox* m_unitCombo = nullptr;

    // Collapsible group boxes
    QGroupBox* m_componentGroup = nullptr;
    QGroupBox* m_positionGroup = nullptr;
    QGroupBox* m_dimensionsGroup = nullptr;
    QGroupBox* m_layoutGroup = nullptr;
    QGroupBox* m_layersGroup = nullptr;
    QGroupBox* m_arrayGroup = nullptr;
    QGroupBox* m_metadataGroup = nullptr;

    // Value labels - Component
    QLabel* m_idLabel = nullptr;
    QLabel* m_typeLabel = nullptr;
    QLabel* m_techLabel = nullptr;
    QLabel* m_connectionLabel = nullptr;

    // Value labels - Position
    QLabel* m_posXLabel = nullptr;
    QLabel* m_posYLabel = nullptr;
    QLabel* m_posZLabel = nullptr;
    QLabel* m_rotZLabel = nullptr;

    // Value labels - Dimensions
    QLabel* m_widthLabel = nullptr;
    QLabel* m_heightLabel = nullptr;
    QLabel* m_thicknessLabel = nullptr;

    // Value labels - Layout
    QLabel* m_layoutPathLabel = nullptr;
    QLabel* m_topCellLabel = nullptr;
    QLabel* m_formatLabel = nullptr;
    QLabel* m_cellCountLabel = nullptr;
    QLabel* m_bboxLabel = nullptr;

    // Layers tree
    QTreeWidget* m_layerTree = nullptr;

    // Value labels - Array
    QLabel* m_arrayPatternLabel = nullptr;
    QLabel* m_arrayCountLabel = nullptr;
    QLabel* m_arrayPitchLabel = nullptr;

    // Metadata tree
    QTreeWidget* m_metadataTree = nullptr;
};

} // namespace chiplet

#endif // CHIPLET_UI_PROPERTIESPANEL_H
