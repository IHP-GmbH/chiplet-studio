// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ImportGdsDialog.cpp - Implementation
 */

#include "ImportGdsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace chiplet {

namespace {

// A read-only path field with a Browse button, laid out as a single row widget.
// Returns the row widget; *edit receives the QLineEdit for later reads.
QWidget* makePathRow(QLineEdit** edit, QWidget* parent, QPushButton** browse)
{
    QWidget* row = new QWidget(parent);
    QHBoxLayout* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    QLineEdit* le = new QLineEdit(row);
    le->setReadOnly(true);
    QPushButton* btn = new QPushButton(QObject::tr("Browse..."), row);
    h->addWidget(le, 1);
    h->addWidget(btn, 0);
    *edit = le;
    *browse = btn;
    return row;
}

}  // namespace

ImportGdsDialog::ImportGdsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Import GDS"));

    QVBoxLayout* root = new QVBoxLayout(this);

    QFormLayout* form = new QFormLayout();

    // GDS picker.
    QPushButton* gdsBrowse = nullptr;
    QWidget* gdsRow = makePathRow(&m_gdsEdit, this, &gdsBrowse);
    m_gdsEdit->setPlaceholderText(tr("Select a .gds file..."));
    connect(gdsBrowse, &QPushButton::clicked, this, &ImportGdsDialog::browseGds);
    form->addRow(tr("GDS file:"), gdsRow);

    // Technology selector: supported PDKs (id in userData) + Custom (empty id).
    m_techCombo = new QComboBox(this);
    m_techCombo->addItem(tr("IHP SG13G2 (130nm BiCMOS)"), QStringLiteral("sg13g2"));
    m_techCombo->addItem(tr("IHP SG13CMOS5L (130nm CMOS)"), QStringLiteral("sg13cmos5l"));
    m_techCombo->addItem(tr("SkyWater SKY130"), QStringLiteral("sky130"));
    m_techCombo->addItem(tr("GlobalFoundries GF180MCU"), QStringLiteral("gf180"));
    m_techCombo->addItem(tr("IHP IntM4TM2 interposer"), QStringLiteral("intm4tm2"));
    m_techCombo->addItem(tr("Custom (unsupported PDK)..."), QString());
    connect(m_techCombo, &QComboBox::currentIndexChanged,
            this, &ImportGdsDialog::onTechChanged);
    form->addRow(tr("Technology:"), m_techCombo);

    // Synthetic die thickness (a 2D GDS carries no Z extent).
    m_thicknessSpin = new QDoubleSpinBox(this);
    m_thicknessSpin->setRange(0.1, 100000.0);
    m_thicknessSpin->setDecimals(1);
    m_thicknessSpin->setValue(200.0);
    m_thicknessSpin->setSuffix(QStringLiteral(" um"));
    form->addRow(tr("Die thickness:"), m_thicknessSpin);

    root->addLayout(form);

    // Custom-PDK files, revealed only for the Custom selection.
    m_customGroup = new QGroupBox(tr("Custom PDK files"), this);
    QFormLayout* customForm = new QFormLayout(m_customGroup);
    QPushButton* lypBrowse = nullptr;
    QWidget* lypRow = makePathRow(&m_lypEdit, m_customGroup, &lypBrowse);
    m_lypEdit->setPlaceholderText(tr("optional .lyp (layer colors / names)"));
    connect(lypBrowse, &QPushButton::clicked, this, &ImportGdsDialog::browseLyp);
    customForm->addRow(tr("Layer props (.lyp):"), lypRow);

    QPushButton* stackupBrowse = nullptr;
    QWidget* stackupRow = makePathRow(&m_stackupEdit, m_customGroup, &stackupBrowse);
    m_stackupEdit->setPlaceholderText(tr("optional stackup YAML (z / thickness)"));
    connect(stackupBrowse, &QPushButton::clicked, this, &ImportGdsDialog::browseStackup);
    customForm->addRow(tr("Stackup YAML:"), stackupRow);

    root->addWidget(m_customGroup);

    QDialogButtonBox* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ImportGdsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ImportGdsDialog::reject);
    root->addWidget(buttons);

    onTechChanged();  // set initial custom-group visibility
}

void ImportGdsDialog::browseGds()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select GDS"), QString(),
        tr("GDS Files (*.gds *.gds2 *.GDS);;All Files (*)"));
    if (!path.isEmpty()) {
        m_gdsEdit->setText(path);
    }
}

void ImportGdsDialog::browseLyp()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select layer properties"), QString(),
        tr("KLayout Layer Properties (*.lyp);;All Files (*)"));
    if (!path.isEmpty()) {
        m_lypEdit->setText(path);
    }
}

void ImportGdsDialog::browseStackup()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select stackup YAML"), QString(),
        tr("Stackup YAML (*.yaml *.yml);;All Files (*)"));
    if (!path.isEmpty()) {
        m_stackupEdit->setText(path);
    }
}

void ImportGdsDialog::onTechChanged()
{
    // Custom is the only entry with an empty technology id.
    const bool custom = m_techCombo->currentData().toString().isEmpty();
    m_customGroup->setVisible(custom);
    adjustSize();
}

void ImportGdsDialog::accept()
{
    const QString gds = m_gdsEdit->text();
    if (gds.isEmpty() || !QFileInfo::exists(gds)) {
        QMessageBox::warning(this, tr("Import GDS"),
                             tr("Select an existing GDS file."));
        return;  // keep the dialog open
    }
    QDialog::accept();
}

QString ImportGdsDialog::gdsPath() const
{
    return m_gdsEdit->text();
}

QString ImportGdsDialog::techId() const
{
    return m_techCombo->currentData().toString();
}

QString ImportGdsDialog::customLyp() const
{
    return m_lypEdit->text();
}

QString ImportGdsDialog::customStackup() const
{
    return m_stackupEdit->text();
}

double ImportGdsDialog::thicknessUm() const
{
    return m_thicknessSpin->value();
}

}  // namespace chiplet
