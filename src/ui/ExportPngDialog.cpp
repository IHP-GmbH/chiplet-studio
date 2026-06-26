// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ExportPngDialog.cpp - Implementation
 */

#include "ExportPngDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtMath>

namespace chiplet {

namespace {
constexpr int kMinDim = 64;
// Conservative upper bound; modern desktop GPUs expose at least this for
// GL_MAX_RENDERBUFFER_SIZE. renderToImage() fails gracefully if it is too big.
constexpr int kMaxDim = 16384;
}  // namespace

ExportPngDialog::ExportPngDialog(const QSize& viewSize, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Export PNG"));

    int baseW = viewSize.width();
    int baseH = viewSize.height();
    if (baseW <= 0 || baseH <= 0) {
        baseW = 1280;
        baseH = 720;
    }
    m_aspect = static_cast<double>(baseW) / static_cast<double>(baseH);

    // Default to a 2x supersample of the current view, clamped to the limit.
    const int defW = qBound(kMinDim, baseW * 2, kMaxDim);
    const int defH = qBound(kMinDim, qRound(defW / m_aspect), kMaxDim);

    QVBoxLayout* root = new QVBoxLayout(this);
    QFormLayout* form = new QFormLayout();

    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(kMinDim, kMaxDim);
    m_widthSpin->setSuffix(QStringLiteral(" px"));
    m_widthSpin->setValue(defW);
    form->addRow(tr("Width:"), m_widthSpin);

    m_heightSpin = new QSpinBox(this);
    m_heightSpin->setRange(kMinDim, kMaxDim);
    m_heightSpin->setSuffix(QStringLiteral(" px"));
    m_heightSpin->setValue(defH);
    form->addRow(tr("Height:"), m_heightSpin);

    m_lockAspect = new QCheckBox(tr("Lock aspect ratio (match the current view)"), this);
    m_lockAspect->setChecked(true);
    form->addRow(QString(), m_lockAspect);

    m_dpiSpin = new QSpinBox(this);
    m_dpiSpin->setRange(72, 2400);
    m_dpiSpin->setSuffix(QStringLiteral(" dpi"));
    m_dpiSpin->setValue(300);
    form->addRow(tr("Print resolution:"), m_dpiSpin);

    m_samplesCombo = new QComboBox(this);
    m_samplesCombo->addItem(tr("Off"), 0);
    m_samplesCombo->addItem(QStringLiteral("2x"), 2);
    m_samplesCombo->addItem(QStringLiteral("4x"), 4);
    m_samplesCombo->addItem(QStringLiteral("8x"), 8);
    m_samplesCombo->setCurrentIndex(2);  // 4x by default
    form->addRow(tr("Anti-aliasing:"), m_samplesCombo);

    m_transparent = new QCheckBox(tr("Transparent background"), this);
    form->addRow(QString(), m_transparent);

    root->addLayout(form);

    QLabel* hint = new QLabel(
        tr("DPI is written to the PNG metadata for print placement; it does not "
           "change the pixel dimensions above."),
        this);
    hint->setWordWrap(true);
    QFont smaller = hint->font();
    smaller.setPointSizeF(smaller.pointSizeF() * 0.9);
    hint->setFont(smaller);
    root->addWidget(hint);

    QDialogButtonBox* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ExportPngDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ExportPngDialog::reject);
    root->addWidget(buttons);

    connect(m_widthSpin, &QSpinBox::valueChanged,
            this, &ExportPngDialog::onWidthChanged);
    connect(m_heightSpin, &QSpinBox::valueChanged,
            this, &ExportPngDialog::onHeightChanged);
}

void ExportPngDialog::onWidthChanged(int w)
{
    if (m_updating || !m_lockAspect->isChecked()) {
        return;
    }
    m_updating = true;
    m_heightSpin->setValue(qBound(kMinDim, qRound(w / m_aspect), kMaxDim));
    m_updating = false;
}

void ExportPngDialog::onHeightChanged(int h)
{
    if (m_updating || !m_lockAspect->isChecked()) {
        return;
    }
    m_updating = true;
    m_widthSpin->setValue(qBound(kMinDim, qRound(h * m_aspect), kMaxDim));
    m_updating = false;
}

QSize ExportPngDialog::outputSize() const
{
    return QSize(m_widthSpin->value(), m_heightSpin->value());
}

int ExportPngDialog::dpi() const
{
    return m_dpiSpin->value();
}

bool ExportPngDialog::transparentBackground() const
{
    return m_transparent->isChecked();
}

int ExportPngDialog::samples() const
{
    return m_samplesCombo->currentData().toInt();
}

}  // namespace chiplet
