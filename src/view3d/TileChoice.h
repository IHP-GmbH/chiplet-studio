// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * TileChoice.h - How the fast 3D view draws each tile this frame, and what
 * the rows and the notes strip say about it
 *
 * One function puts every tile, and so every prepared shape, in exactly one
 * bucket:
 *   shape     in view, and its median record at least upPx on screen (a tile
 *             drawn as shapes last frame stays until it is under downPx);
 *   coverage  in view, but smaller than that, or the budget is spent: nearest
 *             tiles are served first, so the far ones fall back first;
 *   outside   outside the view;
 *   hidden    its layer is switched off.
 * Every shape is the same function with no budget and no threshold. Standard
 * draws only fill this way, and only as coverage: its fill is never a set of
 * shapes cut short in read order.
 *
 * No GL and no Qt: the view, studio-viewcount and the tests share it.
 */

#ifndef CHIPLET_VIEW3D_TILECHOICE_H
#define CHIPLET_VIEW3D_TILECHOICE_H

#include "ShapeTiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chiplet {

// The three settings of View > 3D Shapes. Standard is the default at every
// start and is the view as it was before the other two existed.
enum class ShapeMode {
    Standard,
    Fast,
    Every,
};

struct TileCamera {
    std::array<float, 16> viewProjection{};  // world -> clip, column-major
    std::array<float, 3> eye{};              // world
    float fovYDegrees = 45.0f;
    int viewportHeight = 800;
};

enum class TileBucket : uint8_t {
    Shape,
    Coverage,
    Outside,
    Hidden,
};

struct TileChoiceParams {
    std::size_t budget = 50000;  // records drawn as shapes per frame
    float upPx = 2.0f;           // a tile becomes shapes at this median size on screen
    float downPx = 1.5f;         // and goes back to coverage under this
    bool every = false;          // Every shape: no budget, no threshold
    bool coverageOnly = false;   // every tile in view is coverage (Standard's fill)
};

// What each setting asks of chooseTiles.
TileChoiceParams tileChoiceParams(ShapeMode mode);

struct TileLayerInput {
    const TiledLayer* tiles = nullptr;
    // (x um, y um, z um, 1) in the component's GDS frame -> world,
    // column-major. Its scale is taken from the first column.
    std::array<float, 16> model{};
    float zBottom = 0.0f;  // um, in the same frame
    float zTop = 0.0f;
    bool visible = true;
};

// GDS shapes per bucket for one layer, and what is drawn for them.
struct LayerBuckets {
    std::size_t shape = 0, coverage = 0, outside = 0, hidden = 0;
    std::size_t shapeRecords = 0;
    std::size_t shapeTiles = 0, coverageTiles = 0;

    std::size_t total() const { return shape + coverage + outside + hidden; }
};

struct TileChoice {
    std::vector<std::vector<TileBucket>> buckets;  // per layer, per tile
    std::vector<LayerBuckets> layers;
    std::size_t shapeRecords = 0;   // records drawn as shapes, all layers
    std::size_t coverageTiles = 0;  // quads drawn, all layers
};

// Which tiles were drawn as shapes last frame. A layer list of another
// shape starts it afresh.
struct TileChoiceMemory {
    std::vector<std::vector<uint8_t>> wasShape;
};

TileChoice chooseTiles(const std::vector<TileLayerInput>& layers, const TileCamera& camera,
                       const TileChoiceParams& params, TileChoiceMemory& memory);

// Whether a world box is at least partly in view: the test chooseTiles uses
// for `outside`.
bool worldBoxInView(const std::array<float, 16>& viewProjection, const float lo[3],
                    const float hi[3]);

// ---------------------------------------------------------------------------
// Text. All of it says numbers; none of it relies on colour.

// The notes strip's first line under Fast or Every shape. Empty for Standard,
// whose notes are what they were before the setting existed.
std::string shapeModeNote(ShapeMode mode);

// While the shapes are read for Fast or Every shape.
std::string shapePreparingNote(ShapeMode mode);

// A layer's row under Fast or Every shape:
//   "749,218 shapes; at this zoom 3,912 drawn as shapes, 712,000 as coverage,
//    33,306 outside the view"
//   "64 shapes, all drawn"
//   "55,609 shapes, hidden"
// The parts always add up to `inGds`; shapes the memory limit left out are
// named as a part of their own.
std::string fastRowLabel(std::size_t inGds, const LayerBuckets& buckets);

// One line per component with any coverage:
//   "3D at this zoom: interposer draws 4 layers partly as coverage
//    (1,560,312 of 1,605,829 shapes). Zoom in, or choose Every shape."
// `coverage` counts the coverage shapes and `ofShapes` all shapes on those
// layers. Empty when `layers` is 0.
std::string fastCoverageNote(const std::string& componentId, std::size_t layers,
                             std::size_t coverage, std::size_t ofShapes);

// A layer the memory limit prepared in part:
//   "3D: U1 50/22 Metal4Filler: 40,000 of 55,609 shapes prepared (memory limit)."
// Empty when every shape was prepared.
std::string preparedNote(const std::string& componentId, int layer, int datatype,
                         const std::string& name, std::size_t prepared, std::size_t inGds);

// Every shape's line: "3D: Every shape; the last frame took 4.1 s."
std::string everyShapeNote(double lastFrameSeconds);

// The tooltip line of a row with any coverage.
std::string coverageRowNote();

// Standard with fill on, one line per component with fill in view:
//   "3D: U1 draws 4 fill layers as hatched coverage tiles (1,605,895 shapes
//    in view). Choose Every shape to draw each shape."
// Empty when `layers` is 0.
std::string fillCoverageNote(const std::string& componentId, std::size_t layers,
                             std::size_t shapes);

// Standard with fill on, while the fill shapes are read.
std::string fillReadingNote();

// A component whose shapes Fast could not read; its standard view is drawn:
//   "3D: U1: Fast could not read its shapes (why); its standard view is drawn."
std::string fastReadFailedNote(const std::string& componentId, const std::string& why);

} // namespace chiplet

#endif // CHIPLET_VIEW3D_TILECHOICE_H
