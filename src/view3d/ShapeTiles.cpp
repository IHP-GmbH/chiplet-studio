// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ShapeTiles.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace chiplet {

namespace {

void bboxOf(const TileBox& b, float& x0, float& y0, float& x1, float& y1)
{
    x0 = b.x0; y0 = b.y0; x1 = b.x1; y1 = b.y1;
}

void bboxOf(const TileTrap& t, float& x0, float& y0, float& x1, float& y1)
{
    x0 = std::min(t.xbl, t.xtl);
    x1 = std::max(t.xbr, t.xtr);
    y0 = t.yb;
    y1 = t.yt;
}

float shortSide(const TileBox& b)
{
    return std::min(b.x1 - b.x0, b.y1 - b.y0);
}

float shortSide(const TileTrap& t)
{
    return std::min(t.yt - t.yb, 0.5f * ((t.xbr - t.xbl) + (t.xtr - t.xtl)));
}

float area(const TileBox& b)
{
    return (b.x1 - b.x0) * (b.y1 - b.y0);
}

float area(const TileTrap& t)
{
    return (t.yt - t.yb) * 0.5f * ((t.xbr - t.xbl) + (t.xtr - t.xtl));
}

// Add a record's area to a coverage map over [mx0, mx1] x [my0, my1], spread
// evenly over the record's bounding box and split among the texels it
// overlaps.
template <class R>
void accumulate(const R& r, float mx0, float my0, float mx1, float my1, float* acc)
{
    constexpr int n = kTileMapSide;
    float x0, y0, x1, y1;
    bboxOf(r, x0, y0, x1, y1);
    const float a = area(r);
    if (a <= 0) return;
    const float tw = (mx1 - mx0) / n, th = (my1 - my0) / n;
    if (tw <= 0 || th <= 0) {
        acc[0] += 1.0f;
        return;
    }
    const float bw = x1 - x0, bh = y1 - y0;
    const float density = (bw > 0 && bh > 0) ? a / (bw * bh) : 1.0f;
    const int ix0 = std::clamp(int((x0 - mx0) / tw), 0, n - 1);
    const int ix1 = std::clamp(int((x1 - mx0) / tw), 0, n - 1);
    const int iy0 = std::clamp(int((y0 - my0) / th), 0, n - 1);
    const int iy1 = std::clamp(int((y1 - my0) / th), 0, n - 1);
    for (int iy = iy0; iy <= iy1; ++iy) {
        const float ty0 = my0 + iy * th, ty1 = ty0 + th;
        const float oy = std::max(0.0f, std::min(y1, ty1) - std::max(y0, ty0));
        for (int ix = ix0; ix <= ix1; ++ix) {
            const float tx0 = mx0 + ix * tw, tx1 = tx0 + tw;
            const float ox = std::max(0.0f, std::min(x1, tx1) - std::max(x0, tx0));
            const float part = (bw > 0 && bh > 0) ? ox * oy * density : a;
            acc[iy * n + ix] += part / (tw * th);
        }
    }
}

} // namespace

void LayerRecords::addShape(const std::vector<TileTrap>& parts)
{
    traps.insert(traps.end(), parts.begin(), parts.end());
    trapEnds.push_back(static_cast<uint32_t>(traps.size()));
}

TiledLayer tileLayer(LayerRecords in)
{
    TiledLayer out;
    out.inGds = in.inGds;

    // One item per shape: a box, or another shape with its trapezoids.
    struct Item {
        float cx, cy;
        uint32_t index;    // into boxes, or into trapEnds
        uint32_t records;
        bool box;
    };
    std::vector<Item> items;
    items.reserve(in.shapes());
    for (uint32_t i = 0; i < in.boxes.size(); ++i) {
        const TileBox& b = in.boxes[i];
        items.push_back({0.5f * (b.x0 + b.x1), 0.5f * (b.y0 + b.y1), i, 1, true});
    }
    for (uint32_t i = 0; i < in.trapEnds.size(); ++i) {
        const uint32_t first = i ? in.trapEnds[i - 1] : 0;
        const uint32_t last = in.trapEnds[i];
        float lx = 0, ly = 0, hx = 0, hy = 0;
        for (uint32_t k = first; k < last; ++k) {
            float x0, y0, x1, y1;
            bboxOf(in.traps[k], x0, y0, x1, y1);
            if (k == first) {
                lx = x0; ly = y0; hx = x1; hy = y1;
            } else {
                lx = std::min(lx, x0); ly = std::min(ly, y0);
                hx = std::max(hx, x1); hy = std::max(hy, y1);
            }
        }
        items.push_back({0.5f * (lx + hx), 0.5f * (ly + hy), i, last - first, false});
    }

    // Median splits until a range fits in a tile or holds a single shape.
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    std::vector<std::pair<std::size_t, std::size_t>> stack;
    if (!items.empty()) stack.push_back({0, items.size()});
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        std::size_t recs = 0;
        float lx = items[a].cx, hx = lx, ly = items[a].cy, hy = ly;
        for (std::size_t i = a; i < b; ++i) {
            recs += items[i].records;
            lx = std::min(lx, items[i].cx); hx = std::max(hx, items[i].cx);
            ly = std::min(ly, items[i].cy); hy = std::max(hy, items[i].cy);
        }
        if (recs <= kTileRecords || b - a == 1) {
            ranges.push_back({a, b});
            continue;
        }
        const bool byX = (hx - lx) >= (hy - ly);
        const std::size_t m = a + (b - a) / 2;
        std::nth_element(items.begin() + a, items.begin() + m, items.begin() + b,
                         [byX](const Item& p, const Item& q) {
                             return byX ? p.cx < q.cx : p.cy < q.cy;
                         });
        stack.push_back({m, b});
        stack.push_back({a, m});
    }
    std::sort(ranges.begin(), ranges.end());

    out.boxes.reserve(in.boxes.size());
    out.traps.reserve(in.traps.size());
    out.tiles.reserve(ranges.size());
    std::vector<float> shorts;
    for (const auto& [a, b] : ranges) {
        ShapeTile t;
        t.b0 = out.boxes.size();
        t.t0 = out.traps.size();
        t.shapes = b - a;
        bool first = true;
        auto grow = [&](float x0, float y0, float x1, float y1) {
            if (first) {
                t.x0 = x0; t.y0 = y0; t.x1 = x1; t.y1 = y1;
                first = false;
            } else {
                t.x0 = std::min(t.x0, x0); t.y0 = std::min(t.y0, y0);
                t.x1 = std::max(t.x1, x1); t.y1 = std::max(t.y1, y1);
            }
        };
        shorts.clear();
        for (std::size_t i = a; i < b; ++i) {
            const Item& it = items[i];
            float x0, y0, x1, y1;
            if (it.box) {
                const TileBox& r = in.boxes[it.index];
                out.boxes.push_back(r);
                bboxOf(r, x0, y0, x1, y1);
                grow(x0, y0, x1, y1);
                shorts.push_back(shortSide(r));
                continue;
            }
            const uint32_t from = it.index ? in.trapEnds[it.index - 1] : 0;
            for (uint32_t k = from; k < in.trapEnds[it.index]; ++k) {
                const TileTrap& r = in.traps[k];
                out.traps.push_back(r);
                bboxOf(r, x0, y0, x1, y1);
                grow(x0, y0, x1, y1);
                shorts.push_back(shortSide(r));
            }
        }
        t.b1 = out.boxes.size();
        t.t1 = out.traps.size();
        if (!shorts.empty()) {
            std::nth_element(shorts.begin(), shorts.begin() + shorts.size() / 2, shorts.end());
            t.medianShort = shorts[shorts.size() / 2];
        }
        out.shapes += t.shapes;
        out.tiles.push_back(t);
    }

    out.maps.assign(out.tiles.size() * kTileMapTexels, 0);
    std::vector<float> acc;
    for (std::size_t k = 0; k < out.tiles.size(); ++k) {
        const ShapeTile& t = out.tiles[k];
        acc.assign(kTileMapTexels, 0.0f);
        for (std::size_t i = t.b0; i < t.b1; ++i) {
            accumulate(out.boxes[i], t.x0, t.y0, t.x1, t.y1, acc.data());
        }
        for (std::size_t i = t.t0; i < t.t1; ++i) {
            accumulate(out.traps[i], t.x0, t.y0, t.x1, t.y1, acc.data());
        }
        // A texel any record touches is at least 1, so drawing the tile as
        // coverage leaves none of its shapes out; only empty texels are 0.
        uint8_t* m = out.maps.data() + k * kTileMapTexels;
        for (std::size_t i = 0; i < kTileMapTexels; ++i) {
            const long v = std::lround(std::min(1.0f, acc[i]) * 255.0f);
            m[i] = static_cast<uint8_t>(acc[i] > 0.0f ? std::max(1L, v) : 0L);
        }
    }
    return out;
}

} // namespace chiplet
