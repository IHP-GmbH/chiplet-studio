// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TileChoice.h"

#include "core/CountText.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chiplet {

namespace {

// The six clip planes of a column-major world -> clip matrix (Gribb and
// Hartmann), each as (a, b, c, d) with the inside where ax + by + cz + d >= 0.
void frustumPlanes(const std::array<float, 16>& m, float planes[6][4])
{
    auto at = [&](int row, int col) { return m[col * 4 + row]; };
    for (int i = 0; i < 6; ++i) {
        const int row = i / 2;
        const float sign = (i % 2 == 0) ? 1.0f : -1.0f;
        for (int col = 0; col < 4; ++col) {
            planes[i][col] = at(3, col) + sign * at(row, col);
        }
    }
}

bool boxInFrustum(const float planes[6][4], const float lo[3], const float hi[3])
{
    for (int i = 0; i < 6; ++i) {
        const float* p = planes[i];
        const float x = p[0] >= 0 ? hi[0] : lo[0];
        const float y = p[1] >= 0 ? hi[1] : lo[1];
        const float z = p[2] >= 0 ? hi[2] : lo[2];
        if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0) return false;
    }
    return true;
}

// World bounding box of a local box under a column-major model matrix.
void worldBox(const std::array<float, 16>& m, const float lo[3], const float hi[3],
              float wlo[3], float whi[3])
{
    for (int a = 0; a < 3; ++a) {
        wlo[a] = whi[a] = m[12 + a];
        for (int c = 0; c < 3; ++c) {
            const float e = m[c * 4 + a];
            const float u = e * lo[c], v = e * hi[c];
            wlo[a] += std::min(u, v);
            whi[a] += std::max(u, v);
        }
    }
}

}  // namespace

bool worldBoxInView(const std::array<float, 16>& viewProjection, const float lo[3],
                    const float hi[3])
{
    float planes[6][4];
    frustumPlanes(viewProjection, planes);
    return boxInFrustum(planes, lo, hi);
}

TileChoice chooseTiles(const std::vector<TileLayerInput>& layers, const TileCamera& camera,
                       const TileChoiceParams& params, TileChoiceMemory& memory)
{
    TileChoice out;
    out.buckets.resize(layers.size());
    out.layers.resize(layers.size());

    bool sameShape = memory.wasShape.size() == layers.size();
    for (std::size_t li = 0; sameShape && li < layers.size(); ++li) {
        const std::size_t n = layers[li].tiles ? layers[li].tiles->tiles.size() : 0;
        sameShape = memory.wasShape[li].size() == n;
    }
    if (!sameShape) {
        memory.wasShape.assign(layers.size(), {});
        for (std::size_t li = 0; li < layers.size(); ++li) {
            const std::size_t n = layers[li].tiles ? layers[li].tiles->tiles.size() : 0;
            memory.wasShape[li].assign(n, 0);
        }
    }

    float planes[6][4];
    frustumPlanes(camera.viewProjection, planes);
    const float pxPerUnitAtOne = camera.viewportHeight
        / (2.0f * std::tan(camera.fovYDegrees * 3.14159265358979f / 360.0f));

    struct Candidate {
        float distance;
        uint32_t layer, tile;
    };
    std::vector<Candidate> candidates;

    for (uint32_t li = 0; li < layers.size(); ++li) {
        const TileLayerInput& in = layers[li];
        const std::size_t n = in.tiles ? in.tiles->tiles.size() : 0;
        out.buckets[li].assign(n, TileBucket::Coverage);
        if (n == 0) continue;
        const std::array<float, 16>& m = in.model;
        const float scale = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
        for (uint32_t ti = 0; ti < n; ++ti) {
            const ShapeTile& t = in.tiles->tiles[ti];
            if (!in.visible) {
                out.buckets[li][ti] = TileBucket::Hidden;
                continue;
            }
            const float lo[3] = {t.x0, t.y0, in.zBottom};
            const float hi[3] = {t.x1, t.y1, in.zTop};
            float wlo[3], whi[3];
            worldBox(m, lo, hi, wlo, whi);
            if (!boxInFrustum(planes, wlo, whi)) {
                out.buckets[li][ti] = TileBucket::Outside;
                continue;
            }
            float d2 = 0;
            for (int a = 0; a < 3; ++a) {
                const float c = std::clamp(camera.eye[a], wlo[a], whi[a]);
                d2 += (camera.eye[a] - c) * (camera.eye[a] - c);
            }
            const float distance = std::max(std::sqrt(d2), 1e-9f);
            const float px = t.medianShort * scale * pxPerUnitAtOne / distance;
            const float threshold = memory.wasShape[li][ti] ? params.downPx : params.upPx;
            if (!params.coverageOnly && (params.every || px >= threshold)) {
                candidates.push_back({distance, li, ti});
            }
        }
    }

    // Nearest first, ties in layer and tile order, so the same camera always
    // gives the same choice.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.distance != b.distance) return a.distance < b.distance;
        if (a.layer != b.layer) return a.layer < b.layer;
        return a.tile < b.tile;
    });
    std::size_t used = 0;
    bool spent = false;
    for (const Candidate& c : candidates) {
        const std::size_t records = layers[c.layer].tiles->tiles[c.tile].records();
        if (!params.every && (spent || used + records > params.budget)) {
            spent = true;  // the rest stay coverage
            continue;
        }
        out.buckets[c.layer][c.tile] = TileBucket::Shape;
        used += records;
    }

    for (uint32_t li = 0; li < layers.size(); ++li) {
        LayerBuckets& b = out.layers[li];
        for (uint32_t ti = 0; ti < out.buckets[li].size(); ++ti) {
            const ShapeTile& t = layers[li].tiles->tiles[ti];
            const TileBucket bucket = out.buckets[li][ti];
            memory.wasShape[li][ti] = bucket == TileBucket::Shape ? 1 : 0;
            switch (bucket) {
            case TileBucket::Shape:
                b.shape += t.shapes;
                b.shapeRecords += t.records();
                ++b.shapeTiles;
                break;
            case TileBucket::Coverage:
                b.coverage += t.shapes;
                ++b.coverageTiles;
                break;
            case TileBucket::Outside:
                b.outside += t.shapes;
                break;
            case TileBucket::Hidden:
                b.hidden += t.shapes;
                break;
            }
        }
        out.shapeRecords += b.shapeRecords;
        out.coverageTiles += b.coverageTiles;
    }
    return out;
}

TileChoiceParams tileChoiceParams(ShapeMode mode)
{
    TileChoiceParams params;
    params.every = mode == ShapeMode::Every;
    params.coverageOnly = mode == ShapeMode::Standard;
    return params;
}

std::string shapeModeNote(ShapeMode mode)
{
    switch (mode) {
    case ShapeMode::Standard:
        return {};
    case ShapeMode::Fast:
        return "3D: Fast";
    case ShapeMode::Every:
        return "3D: Every shape";
    }
    return {};
}

std::string shapePreparingNote(ShapeMode mode)
{
    const std::string name = mode == ShapeMode::Every ? "Every shape" : "Fast";
    return "3D: reading every shape for " + name + "; the standard view is shown until then.";
}

std::string fastRowLabel(std::size_t inGds, const LayerBuckets& b)
{
    const std::size_t prepared = b.total();
    const std::string total = groupedCount(inGds) + " shapes";
    std::string missing;
    if (prepared < inGds) {
        missing = groupedCount(inGds - prepared) + " not prepared (memory limit)";
    }
    if (b.hidden == prepared && prepared > 0) {
        return total + ", hidden" + (missing.empty() ? "" : "; " + missing);
    }
    if (b.shape == prepared && missing.empty()) {
        return total + ", all drawn";
    }
    std::string out = total + "; at this zoom " + groupedCount(b.shape) + " drawn as shapes, "
                    + groupedCount(b.coverage) + " as coverage, " + groupedCount(b.outside)
                    + " outside the view";
    if (b.hidden) {
        out += ", " + groupedCount(b.hidden) + " hidden";
    }
    if (!missing.empty()) {
        out += ", " + missing;
    }
    return out;
}

std::string fastCoverageNote(const std::string& componentId, std::size_t layers,
                             std::size_t coverage, std::size_t ofShapes)
{
    if (layers == 0) {
        return {};
    }
    return "3D at this zoom: " + componentId + " draws " + std::to_string(layers)
         + (layers == 1 ? " layer" : " layers") + " partly as coverage ("
         + groupedCount(coverage) + " of " + groupedCount(ofShapes)
         + " shapes). Zoom in, or choose Every shape.";
}

std::string preparedNote(const std::string& componentId, int layer, int datatype,
                         const std::string& name, std::size_t prepared, std::size_t inGds)
{
    if (prepared >= inGds) {
        return {};
    }
    std::string note = "3D: " + componentId + " " + std::to_string(layer) + "/"
                     + std::to_string(datatype);
    if (!name.empty()) {
        note += " " + name;
    }
    return note + ": " + groupedCount(prepared) + " of " + groupedCount(inGds)
         + " shapes prepared (memory limit).";
}

std::string everyShapeNote(double lastFrameSeconds)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", lastFrameSeconds);
    return std::string("3D: Every shape; the last frame took ") + buf + " s.";
}

std::string fastReadFailedNote(const std::string& componentId, const std::string& why)
{
    return "3D: " + componentId + ": Fast could not read its shapes (" + why
         + "); its standard view is drawn.";
}

std::string fillCoverageNote(const std::string& componentId, std::size_t layers,
                             std::size_t shapes)
{
    if (layers == 0) {
        return {};
    }
    return "3D: " + componentId + " draws " + std::to_string(layers)
         + (layers == 1 ? " fill layer" : " fill layers") + " as hatched coverage tiles ("
         + groupedCount(shapes) + " shapes in view). Choose Every shape to draw each shape.";
}

std::string fillReadingNote()
{
    return "3D: reading every fill shape; fill is drawn once it is read.";
}

std::string coverageRowNote()
{
    return "Coverage shows where the layer's shapes are, not their outlines. Zoom in, or "
           "choose Every shape.";
}

} // namespace chiplet
