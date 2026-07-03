// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * SceneMiniMap.h - Corner top-down mini-map (bird's-eye) for the 3D view.
 *
 * The 3D analogue of the 2D OverviewNavigator: a small floor-plan of the whole
 * assembly seen from above (each component as a colored rectangle on the scene
 * X-Z plane), with a marker showing where the camera is looking. Click or drag
 * to slide the 3D camera's look-at across the system; wheel to zoom. Modeled on
 * the "world view" / aerial navigator of commercial PCB/IC layout tools.
 *
 * Design: pure Qt widget. It talks to the 3D view only through AssemblyView's
 * scene/GL-free API (SceneOverview / doubles) and delegates coordinate math to
 * OverviewTransform (shared with the 2D navigator) plus the floor convention in
 * SceneOverview.h, so no OpenGL or scene-math type ever reaches here. The
 * footprint map is a cached vector model refreshed on content changes; only the
 * camera marker updates during orbit/pan/zoom.
 */

#ifndef CHIPLET_VIEW3D_SCENEMINIMAP_H
#define CHIPLET_VIEW3D_SCENEMINIMAP_H

#include <QWidget>
#include <QPoint>

#include "SceneOverview.h"
#include "view2d/OverviewTransform.h"

namespace chiplet {

class AssemblyView;

/**
 * @brief Corner top-down mini-map / navigator for an AssemblyView.
 */
class SceneMiniMap : public QWidget {
    Q_OBJECT

public:
    /**
     * @param view The 3D view this mini-map mirrors and drives (not owned).
     */
    explicit SceneMiniMap(AssemblyView* view, QWidget* parent = nullptr);

    /// True when there is a valid floor-plan to show (an assembly with geometry).
    /// The owner uses this to hide the overlay on an empty scene.
    bool hasContent() const { return m_data.valid(); }

public slots:
    /// Rebuild the footprint floor-plan (assembly set/cleared, meshes rebuilt,
    /// selection change).
    void onContentChanged();

    /// Cheap refresh of the camera marker only (orbit/pan/zoom).
    void onCameraChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    /// Inner pixel rectangle the map is drawn into (minus frame pad + title).
    OverviewPixelRect drawArea() const;

    /// Transform for the current floor bounds and draw area (invalid if empty).
    OverviewTransform transform() const;

    /// Drive the 3D camera from a widget-pixel position (click/drag).
    void navigateFromPixel(const QPoint& pos);

    AssemblyView* m_view = nullptr;
    SceneOverview m_data;        // cached floor-plan + camera marker
    bool          m_pressed = false;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_SCENEMINIMAP_H
