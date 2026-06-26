// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ImportGdsDialog.h - Collect inputs for File > Import GDS.
 *
 * Picks a standalone GDS plus the PDK to render it with. A supported PDK is
 * chosen from a list (the studio resolves its stackup + colors from the id); a
 * "Custom..." choice reveals fields for a user-supplied .lyp and/or stackup
 * YAML, for closed / unsupported nodes. The caller reads the accessors after
 * exec() == QDialog::Accepted and synthesizes a minimal .chiplet from them.
 */

#ifndef CHIPLET_UI_IMPORT_GDS_DIALOG_H
#define CHIPLET_UI_IMPORT_GDS_DIALOG_H

#include <QDialog>

class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;

namespace chiplet {

class ImportGdsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ImportGdsDialog(QWidget* parent = nullptr);

    /// Absolute path to the selected GDS.
    QString gdsPath() const;
    /// Supported-PDK technology id, or an empty string when "Custom..." is chosen.
    QString techId() const;
    /// Verbatim .lyp path for a custom PDK (meaningful only when techId() is empty).
    QString customLyp() const;
    /// Verbatim stackup YAML path for a custom PDK (meaningful only when techId() is empty).
    QString customStackup() const;
    /// Synthetic die thickness in micrometers.
    double thicknessUm() const;

protected:
    void accept() override;

private slots:
    void browseGds();
    void browseLyp();
    void browseStackup();
    void onTechChanged();

private:
    QLineEdit* m_gdsEdit = nullptr;
    QComboBox* m_techCombo = nullptr;
    QDoubleSpinBox* m_thicknessSpin = nullptr;
    QGroupBox* m_customGroup = nullptr;
    QLineEdit* m_lypEdit = nullptr;
    QLineEdit* m_stackupEdit = nullptr;
};

}  // namespace chiplet

#endif  // CHIPLET_UI_IMPORT_GDS_DIALOG_H
