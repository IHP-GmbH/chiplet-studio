// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ExportPngDialog.h - Collect inputs for File > Export PNG.
 *
 * Picks the output resolution (independent of the on-screen view size), an
 * optional DPI written to the PNG metadata, anti-aliasing samples, and whether
 * the background is transparent. The caller reads the accessors after exec() ==
 * QDialog::Accepted and renders the 3D view offscreen at outputSize().
 */

#ifndef CHIPLET_UI_EXPORT_PNG_DIALOG_H
#define CHIPLET_UI_EXPORT_PNG_DIALOG_H

#include <QDialog>
#include <QSize>

class QSpinBox;
class QCheckBox;
class QComboBox;

namespace chiplet {

class ExportPngDialog : public QDialog {
    Q_OBJECT

public:
    // viewSize seeds the default resolution and the aspect ratio that the
    // "lock aspect" option preserves (typically the AssemblyView framebuffer
    // size). A degenerate size falls back to 16:9.
    explicit ExportPngDialog(const QSize& viewSize, QWidget* parent = nullptr);

    /// Pixel dimensions to render the capture at.
    QSize outputSize() const;
    /// DPI written to the PNG metadata (does not change the pixel size).
    int dpi() const;
    /// Clear to a 0-alpha background when true.
    bool transparentBackground() const;
    /// MSAA samples (0 = off).
    int samples() const;

private slots:
    void onWidthChanged(int w);
    void onHeightChanged(int h);

private:
    double m_aspect = 16.0 / 9.0;  // width / height of the source view
    bool m_updating = false;       // re-entrancy guard for the aspect link

    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QCheckBox* m_lockAspect = nullptr;
    QSpinBox* m_dpiSpin = nullptr;
    QCheckBox* m_transparent = nullptr;
    QComboBox* m_samplesCombo = nullptr;
};

}  // namespace chiplet

#endif  // CHIPLET_UI_EXPORT_PNG_DIALOG_H
