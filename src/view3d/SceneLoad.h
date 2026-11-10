// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * SceneLoad.h - The CPU half of loading the 3D view
 *
 * Everything AssemblyView computes from an assembly before any of it goes to
 * GL: the layer sheets, each technology's render stackup, the box meshes,
 * and per component the standard view's layer meshes, shape counts and
 * notes. The fast view's tiles are read here too, from the same placement.
 *
 * studio-viewcount runs these same functions with no display and no GL, so
 * what it prints is what the view was handed.
 */

#ifndef CHIPLET_VIEW3D_SCENELOAD_H
#define CHIPLET_VIEW3D_SCENELOAD_H

#include "ComponentMesh.h"
#include "GDSLayerExtractor.h"
#include "LayerMeshBuilder.h"
#include "ShapeFilter.h"
#include "ShapeTiles.h"
#include "TileChoice.h"
#include "core/LayerStackup.h"
#include "core/TechResolve.h"
#include "view2d/LayerProperties.h"
#include "math/Maths.h"

#include <QMatrix4x4>
#include <QString>
#include <QVector2D>

#include <array>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace chiplet {

class Assembly;
class Component;

// Each technology's .lyp, and a line for each one that is there and does not
// read (a missing one is already a technology note).
struct LayerSheets {
    std::map<std::string, LayerPropertiesFile> props;  // by technology id
    std::vector<std::string> notes;
};

LayerSheets loadLayerSheets(const Assembly& assembly);

// The .lyp of `techId`, or null.
const LayerPropertiesFile* findSheet(const std::map<std::string, LayerPropertiesFile>& sheets,
                                     const std::string& techId);

// The stackup a technology's layers are drawn at, before any black-box layer
// is added for one component.
struct RenderStackup {
    LayerStackup stackup;
    bool modeled = false;  // from a stackup file, not derived from the .lyp or the generic one
    std::string label;     // what the notes call it (stackupLabel)
};

RenderStackup resolveRenderStackup(const Assembly& assembly, const std::string& techId,
                                   const LayerPropertiesFile* lyp);

// The stackup a technology is drawn at, resolved on first use and kept in
// `cache` by technology id. The scene load fills the cache and the view keeps
// it, so a die built later and a Filter rebuild draw at the stackup the load
// used, the interconnect bodies on the file's plane included.
const RenderStackup& cachedRenderStackup(std::map<std::string, RenderStackup>& cache,
                                         const Assembly& assembly, const std::string& techId,
                                         const LayerPropertiesFile* lyp);

// The standard view's extraction limits for a stackup.
ExtractionConfig standardExtractionConfig(const LayerStackup& stackup);

// A die with neither a .lyp nor a stackup file: give its own layers a height
// in a local copy of the stackup, so it draws as a body with pads.
void augmentStackupForBlackBox(LayerStackup& stackup,
                               const std::map<LayerKey, LayerPolygons>& polygons,
                               const Component& comp);

// Chiplet frame -> scene: the component's position, mirrored in X face down.
QMatrix4x4 componentTransform(const Component& comp);

// One box mesh per component, from its GDS extent where it has one.
std::map<QString, ComponentMesh> buildBoxMeshes(const Assembly& assembly,
                                                const std::map<std::string, LayerPropertiesFile>& sheets);

// The standard view's layer meshes for `polygons`, with what was needed to
// place them.
struct StandardBuild {
    Component3DGeometry geometry;  // transform not set
    LayerStackup stackup;          // the render stackup, plus any black-box layers
    std::optional<LayerColorScheme> colorScheme;
    bool flipZ = false;
    double beolTop = 0.0;
};

StandardBuild buildStandardGeometry(const Component& comp,
                                    const std::map<LayerKey, LayerPolygons>& polygons,
                                    const LayerPropertiesFile* lyp,
                                    const RenderStackup& render);

// What the fast view reads for a component: every GDS layer the standard
// view places, at the standard view's height and colour. A stackup that
// declares the substrate gets the standard view's full-extent body instead,
// so 40/0 is not read.
struct FastSource {
    std::string componentId;
    std::string layoutPath;
    std::string topCell;
    std::vector<std::pair<LayerKey, LayerPlacement>> layers;
};

struct StandardComponent {
    Component3DGeometry geometry;                 // CPU only; upload before drawing
    std::map<LayerKey, LayerPolygons> polygons;   // as read, before any shape filter
    AreaStatistics areaStats;
    std::map<LayerKey, LayerShapeTally> tally;
    std::vector<ShapeCount> counts;
    std::string note;                             // layers placed nowhere; may be empty
    LayerStackup stackup;                         // as placed
    FastSource fast;
};

// Empty when the component has no layout or nothing could be read.
std::optional<StandardComponent> loadStandardComponent(const Component& comp,
                                                       const LayerPropertiesFile* lyp,
                                                       const RenderStackup& render,
                                                       double filterPercent);

struct StandardScene {
    std::map<QString, ComponentMesh> boxes;
    std::map<std::string, RenderStackup> stackups;  // by technology id
    std::map<QString, StandardComponent> components;
};

// What AssemblyView loads for an assembly with Standard, in its order.
// `filterPercent` is the shape filter per component (absent = none).
StandardScene loadStandardScene(const Assembly& assembly,
                                const std::map<std::string, LayerPropertiesFile>& sheets,
                                const std::map<QString, double>& filterPercent);

// The world box of every layer mesh under its component transform, and of
// every box mesh; a default box when there is neither.
class SceneBounds {
public:
    void add(const Component3DGeometry& geometry);
    void add(const ComponentMesh& mesh);
    AA_BOUNDING_BOX box() const;

private:
    AA_BOUNDING_BOX m_box;
    bool m_empty = true;
};

// ---------------------------------------------------------------------------
// The fast view's tiles.

struct FastLayer {
    LayerKey key;
    LayerPlacement place;
    TiledLayer tiles;
};

struct FastComponent {
    std::string componentId;
    std::vector<FastLayer> layers;  // by height, as the standard meshes
    std::vector<std::string> notes; // a layer the memory limit prepared in part
    bool fillOnly = false;          // read for Standard's fill: its fill layers alone
};

// Read and tile every shape of `source`'s layers. `recordBudget` is what all
// components together may still prepare (kMaxPreparedRecords at the start);
// it is spent in component, then layer order.
FastComponent loadFastComponent(const FastSource& source, std::size_t& recordBudget);

// `source` with its fill layers alone; none when it has no fill.
FastSource fillSource(const FastSource& source);

// What Standard draws fill from, with fill on: every shape of the fill
// layers, from the same budget, never the standard read's first shapes.
FastComponent loadFillTiles(const FastSource& source, std::size_t& recordBudget);

// (x um, y um, z um) in the component's GDS frame -> world, column-major:
// the standard meshes' centring and mm units, the component transform, and
// `spreadMm` up along the layer's own axis (the layer Z spacing).
std::array<float, 16> fastLayerModel(const QMatrix4x4& transform,
                                     const QVector2D& centeringOffsetMm, float spreadMm);

// One chooseTiles input per fast layer of every component: which component
// and layer each entry is.
struct FastFrameLayer {
    std::string componentId;
    const FastLayer* layer = nullptr;
};

// A component as the fast view draws it this frame.
struct FastFramePart {
    const FastComponent* fast = nullptr;
    const Component3DGeometry* geometry = nullptr;  // its transform and centring
    bool shown = false;          // visible and detailed, so its layers can draw
    bool hideSubstrate = false;  // detailed without substrate
};

// Every fast layer of `parts`, in order, as chooseTiles takes it: which
// component and layer each entry is, its input, and its rank among its
// component's layers with the same top (0 for the first). A layer draws when
// its part is shown, it is not a hidden substrate, and `layerShown` says so.
// `spreadFactor` is the layer Z spacing (1 = none), moving each layer as the
// standard view moves its mesh.
struct FastFrame {
    std::vector<FastFrameLayer> refs;
    std::vector<TileLayerInput> inputs;
    std::vector<int> coverRanks;
};

FastFrame fastFrame(const std::vector<FastFramePart>& parts,
                    const std::function<bool(const std::string& componentId,
                                             const FastLayer& layer)>& layerShown,
                    float spreadFactor);

// Per component with any coverage this frame, its line for the notes strip
// (fastCoverageNote). `refs[i]` is the layer of `choice.layers[i]`.
std::map<std::string, std::string> coverageNotes(const std::vector<FastFrameLayer>& refs,
                                                 const TileChoice& choice);

// The same for Standard's fill frame: per component with fill in view, its
// fillCoverageNote.
std::map<std::string, std::string> fillCoverageNotes(const std::vector<FastFrameLayer>& refs,
                                                     const TileChoice& choice);

// A component's layer rows with Fast or Every shape: the standard rows, each
// layer the fast view prepared with its buckets' label (fastRowLabel) and a
// checkbox, since the fast view draws it. `buckets` holds this component's
// layers as last settled; a layer not in it gets no count yet. Any coverage
// adds coverageRowNote to the tooltip. With `fillOnly` (Standard with fill
// on) only the fill layers are taken from `fast`.
std::vector<LayerRow> fastLayerRows(std::vector<LayerRow> standard, const FastComponent& fast,
                                    const std::map<LayerKey, LayerBuckets>& buckets,
                                    bool fillOnly = false);

// ---------------------------------------------------------------------------
// The notes strip.

// With Standard: sheets that would not read, then per component in assembly
// order the layers placed nowhere and the count notes. A fill layer has none:
// its standard mesh is never drawn.
std::vector<std::string> standardNotes(const Assembly& assembly,
                                       const std::vector<std::string>& sheetNotes,
                                       const std::map<QString, std::string>& componentNotes,
                                       const std::map<QString, std::vector<ShapeCount>>& counts);

// With Fast or Every shape: the setting first, the sheets, then per
// component the layers placed nowhere, what the memory limit left out, and
// this frame's coverage line. The standard view's count notes are not
// shown: its limits do not apply.
std::vector<std::string> fastNotes(ShapeMode mode, const Assembly& assembly,
                                   const std::vector<std::string>& sheetNotes,
                                   const std::map<QString, std::string>& componentNotes,
                                   const std::map<QString, FastComponent>& fast,
                                   const std::map<std::string, std::string>& coverage);

// A component that has layers but is drawn as a body: 3D draws layers, fill
// and Fast or Every shapes only for a Detailed component. `fill` says one of
// its layers is fill; `faceDown` that its Si bulk lies over its layers as the
// view opens.
struct UndetailedPart {
    std::string componentId;
    bool fill = false;
    bool faceDown = false;
};

// In assembly order, each component with layer geometry whose render mode is
// neither Detailed nor Hidden.
std::vector<UndetailedPart> undetailedParts(const Assembly& assembly,
                                            const std::map<QString, Component3DGeometry>& geometry);

// The line naming the parts whose layers a switched-on setting cannot show:
// fill for a part with fill, Fast or Every shape for any part. Empty when
// there is none, so the view as it opens (fill off, Standard) says nothing.
//   "3D: U1, U2 are not drawn Detailed, so their layers, fill and 3D Shapes
//    do not show. Right-click them in Hierarchy, Render Mode, Detailed."
// With a face-down part the last sentence adds that it needs Detailed (no Si
// bulk) to show them from above.
std::string detailNote(ShapeMode mode, bool showFill, const std::vector<UndetailedPart>& parts);

// What the view's notes strip says for its setting:
//   Standard     standardNotes; with fill on, fillReadingNote until every
//                component's fill is read, then per component what the
//                memory limit left out of its fill and the fill frame's line
//                from `coverage` (fillCoverageNotes);
//   reading      the setting, shapePreparingNote, then standardNotes, since
//                the standard view is drawn until the shapes are read;
//   prepared     fastNotes, and under Every shape the first line names the
//                last frame's time once a frame was timed (seconds >= 0).
// In each, detailNote comes first when it says anything.
struct ViewNotesState {
    ShapeMode mode = ShapeMode::Standard;
    bool prepared = false;
    double everyFrameSeconds = -1.0;
    bool showFill = false;
    // Standard: no component whose fill the view draws (Detailed and shown)
    // waits for its fill read. True when there is none, so the strip never
    // says fill is coming for a die it names as not Detailed.
    bool fillPrepared = true;
    std::vector<UndetailedPart> undetailed;
};

std::vector<std::string> viewNotes(const ViewNotesState& state, const Assembly& assembly,
                                   const std::vector<std::string>& sheetNotes,
                                   const std::map<QString, std::string>& componentNotes,
                                   const std::map<QString, std::vector<ShapeCount>>& counts,
                                   const std::map<QString, FastComponent>& fast,
                                   const std::map<std::string, std::string>& coverage);

} // namespace chiplet

#endif // CHIPLET_VIEW3D_SCENELOAD_H
