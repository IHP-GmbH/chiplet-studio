/**
 * PropertiesPanel.cpp - Implementation
 */

#include "PropertiesPanel.h"
#include <QFormLayout>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QVBoxLayout>

namespace chiplet {

PropertiesPanel::PropertiesPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

PropertiesPanel::~PropertiesPanel() = default;

void PropertiesPanel::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    m_form = new QFormLayout();
    mainLayout->addLayout(m_form);
    mainLayout->addStretch();
}

void PropertiesPanel::setComponent(Component* component)
{
    m_component = component;
    refresh();
}

void PropertiesPanel::refresh()
{
    // Clear existing form
    while (m_form->rowCount() > 0) {
        m_form->removeRow(0);
    }

    if (!m_component) {
        m_form->addRow(new QLabel("No component selected"));
        return;
    }

    // ID (read-only)
    QLineEdit* idEdit = new QLineEdit(QString::fromStdString(m_component->id()));
    idEdit->setReadOnly(true);
    m_form->addRow("ID:", idEdit);

    // Technology
    QLineEdit* techEdit = new QLineEdit(QString::fromStdString(m_component->technology()));
    techEdit->setReadOnly(true);
    m_form->addRow("Technology:", techEdit);

    // Position
    const Position3D& pos = m_component->position();
    m_form->addRow("Position X:", new QLabel(QString::number(pos.x)));
    m_form->addRow("Position Y:", new QLabel(QString::number(pos.y)));
    m_form->addRow("Position Z:", new QLabel(QString::number(pos.z)));

    // TODO: Add editable fields with proper change handling
}

} // namespace chiplet
