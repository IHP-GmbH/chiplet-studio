/**
 * PropertiesPanel.h - Property editor for selected component
 */

#ifndef CHIPLET_UI_PROPERTIESPANEL_H
#define CHIPLET_UI_PROPERTIESPANEL_H

#include <QWidget>
#include "core/Component.h"

class QFormLayout;
class QLineEdit;
class QDoubleSpinBox;

namespace chiplet {

/**
 * PropertiesPanel displays and edits properties of selected component.
 */
class PropertiesPanel : public QWidget {
    Q_OBJECT

public:
    PropertiesPanel(QWidget* parent = nullptr);
    ~PropertiesPanel();

    void setComponent(Component* component);

signals:
    void componentModified(const QString& componentId);

private:
    void setupUI();
    void refresh();

    QFormLayout* m_form = nullptr;
    Component* m_component = nullptr;
};

} // namespace chiplet

#endif // CHIPLET_UI_PROPERTIESPANEL_H
