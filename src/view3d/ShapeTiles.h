// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * ShapeTiles.h - Every shape of a layer, cut into tiles, for the fast 3D view
 *
 * The fast view reads every shape of a layer, with none of the standard
 * view's limits. A box is one record; any other shape is the horizontal
 * trapezoids it decomposes into, one record each. A layer is then cut into
 * tiles of at most kTileRecords records, and each tile keeps what the
 * per-frame choice needs (its bounding box, the median short side of its
 * records) and a coverage map for when it is drawn as coverage.
 *
 * No GL and no Qt: the view, studio-viewcount and the tests share it.
 */

#ifndef CHIPLET_VIEW3D_SHAPETILES_H
#define CHIPLET_VIEW3D_SHAPETILES_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chiplet {

// Coordinates in um, in the component's GDS frame.
struct TileBox {
    float x0, y0, x1, y1;
};

// A horizontal trapezoid: bottom edge at yb from xbl to xbr, top edge at yt
// from xtl to xtr.
struct TileTrap {
    float yb, yt, xbl, xbr, xtl, xtr;
};

// One layer's shapes as read for the fast view, before tiling.
struct LayerRecords {
    std::vector<TileBox> boxes;      // one per box shape
    std::vector<TileTrap> traps;     // the trapezoids of every other shape
    std::vector<uint32_t> trapEnds;  // other shape i owns traps [trapEnds[i-1], trapEnds[i])
    std::size_t inGds = 0;           // shapes on the layer in the GDS, prepared or not

    std::size_t shapes() const { return boxes.size() + trapEnds.size(); }
    std::size_t records() const { return boxes.size() + traps.size(); }

    // A shape that is not a box, as its trapezoids (possibly none).
    void addShape(const std::vector<TileTrap>& parts);
};

inline constexpr std::size_t kTileRecords = 2000;
inline constexpr int kTileMapSide = 32;
inline constexpr std::size_t kTileMapTexels = kTileMapSide * kTileMapSide;

// What all components together may prepare: records are 16 or 24 bytes, so
// this keeps the fast view's own copy under about 768 MB. A layer past it is
// prepared in part, and its row and note say how much.
inline constexpr std::size_t kMaxPreparedRecords = std::size_t(32) << 20;

struct ShapeTile {
    std::size_t b0 = 0, b1 = 0;    // range in TiledLayer::boxes
    std::size_t t0 = 0, t1 = 0;    // range in TiledLayer::traps
    std::size_t shapes = 0;        // GDS shapes whose records are in the tile
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // bounding box of its records, um
    float medianShort = 0;         // median short side of its records, um

    std::size_t records() const { return (b1 - b0) + (t1 - t0); }
};

struct TiledLayer {
    std::vector<TileBox> boxes;
    std::vector<TileTrap> traps;
    std::vector<ShapeTile> tiles;
    // kTileMapTexels per tile, in tile order: the fraction of each texel its
    // records cover, 1 to 255 where any record is, 0 where none is; rows from
    // the tile's y0 up.
    std::vector<uint8_t> maps;
    std::size_t inGds = 0;   // shapes on the layer in the GDS
    std::size_t shapes = 0;  // shapes prepared, in some tile

    std::size_t records() const { return boxes.size() + traps.size(); }
    const uint8_t* map(std::size_t tile) const { return maps.data() + tile * kTileMapTexels; }
};

// Cut a layer into tiles: recursive median splits on the longer axis of the
// shape centres until a range holds at most kTileRecords records (or one
// shape), so a shape's trapezoids stay in one tile. Records are reordered so
// each tile is one range of boxes and one of trapezoids.
TiledLayer tileLayer(LayerRecords records);

} // namespace chiplet

#endif // CHIPLET_VIEW3D_SHAPETILES_H
