// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * ViewCount.h - What the 3D view hands to GL, counted without GL
 *
 * studio-viewcount loads an assembly through the view's own loading code
 * (SceneLoad), runs the view's per-frame choice for a camera, and checks
 * that every shape in each GDS is accounted for:
 *   standard  every shape the standard view leaves out is named by a note;
 *             a fill layer is hidden with fill off, and with fill on every
 *             fill shape is prepared and, with a camera, drawn as coverage
 *             tiles, none as shapes, with its line in the notes strip;
 *   fast/every every shape is prepared, or a note names the ones the memory
 *             limit left out; with a camera, each layer's buckets add up to
 *             what was prepared and every component with coverage has its
 *             line in the notes strip.
 * The checks only read what the view would show; a failure names the layer.
 */

#ifndef CHIPLET_VIEW3D_VIEWCOUNT_H
#define CHIPLET_VIEW3D_VIEWCOUNT_H

#include "Camera.h"
#include "SceneLoad.h"
#include "TileChoice.h"

#include "core/Component.h"

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace chiplet {

class Assembly;

enum class ViewCountCamera {
    None,      // counts only
    Fit,       // the camera the view opens at: top down, the whole assembly
    Mid,       // oblique, fitted to the first die
    Close,     // oblique, at a tenth of the fit distance, on the interposer's top
               // face where nothing stands between the eye and it
    Explicit,  // eye and target given
};

struct ViewCountOptions {
    ShapeMode mode = ShapeMode::Standard;
    ViewCountCamera camera = ViewCountCamera::None;
    std::array<float, 3> eye{}, target{};  // world, mm (Explicit)
    int width = 1280;
    int height = 800;
    bool fill = false;  // fill layers shown; the view opens with them off
    std::size_t recordBudget = kMaxPreparedRecords;  // what Fast may prepare, all components
};

// The oblique direction of the mid and close cameras (degrees).
inline constexpr float kObliqueYaw = 45.0f;
inline constexpr float kObliquePitch = 35.0f;

struct ViewCountReport {
    std::vector<std::string> lines;     // the report
    std::vector<std::string> failures;  // empty when every check passes
};

// Where the close camera looks: the cell of its grid on a component's top
// face, and how many cells it tried, that one included.
struct CloseCell {
    std::string face;
    int i = -1, j = -1;
    int tried = 0;
};

// The camera `options` names for this scene, as the view would hold it at
// that viewport. False, with `error` set, when it cannot be placed (no die
// for Mid, no clear point for Close, an eye on the target). For Close, `cell`
// (when given) says where it looks.
bool viewCountCamera(const Assembly& assembly, const StandardScene& scene,
                     const ViewCountOptions& options, Camera& camera, std::string& error,
                     CloseCell* cell = nullptr);

// A component's layers box (when it has layers) or its body box, in world mm.
struct ComponentBox {
    std::string id;
    std::string kind;  // "layers" or "body"
    AA_BOUNDING_BOX box;
};

// Every layers box, by component id, then every body box: what the close
// camera's eye must stay out of and see its target past.
std::vector<ComponentBox> componentBoxes(const StandardScene& scene);

// The choice's view of a camera at a viewport.
TileCamera tileCamera(const Camera& camera, int width, int height);

// The render mode a command line names: hidden, wireframe, transparent,
// solid, detailed or detailed-no-si. Nothing for any other name.
std::optional<RenderMode> renderModeNamed(const std::string& name);

// Set each component `modes` names (id to a mode name) to that mode. False,
// with `error` naming the first id the assembly lacks or the first name that
// is no mode, and then no component is changed.
bool setRenderModes(Assembly& assembly, const std::map<std::string, std::string>& modes,
                    std::string& error);

// Load `assembly` as the view does and count it.
ViewCountReport viewCount(const Assembly& assembly, const ViewCountOptions& options);

// Count a scene already loaded with loadStandardScene (or put together the
// same way); `sheetNotes` are LayerSheets::notes.
ViewCountReport viewCountScene(const Assembly& assembly, StandardScene scene,
                               const std::vector<std::string>& sheetNotes,
                               const ViewCountOptions& options);

} // namespace chiplet

#endif // CHIPLET_VIEW3D_VIEWCOUNT_H
