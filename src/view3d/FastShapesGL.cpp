// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FastShapesGL.h"
#include "SceneManager.h"

#include <QDebug>

#include <algorithm>
#include <cstdint>
#include <utility>

#ifndef GL_PRIMITIVES_GENERATED
#define GL_PRIMITIVES_GENERATED 0x8C87
#endif

namespace chiplet {

namespace {

// Coverage maps: kTileMapSide texels a tile, kAtlasTiles tiles to a row and
// to a column of each page of the texture array.
constexpr int kAtlasTiles = 64;
constexpr int kAtlasSide = kAtlasTiles * kTileMapSide;
constexpr std::size_t kTilesPerPage = std::size_t(kAtlasTiles) * kAtlasTiles;

// A cover instance: the tile's box (4) and its place in the atlas (3).
constexpr std::size_t kCoverFloats = 7;

// The view's component shader, with the record expanded in the vertex
// shader: a unit prism stretched to the record in x and y and to the
// layer's height in z, in the component's GDS frame (um).
const char* kBoxVert = R"(#version 330 core
layout(location = 0) in vec3 unitPos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 rect;   // x0, y0, x1, y1
uniform mat4 modelViewProjection;
uniform mat3 normalMatrix;
uniform float zBottom;
uniform float thickness;
out vec3 fragNormal;
out float flogz;
void main() {
    vec3 position = vec3(mix(rect.x, rect.z, unitPos.x),
                         mix(rect.y, rect.w, unitPos.y),
                         zBottom + unitPos.z * thickness);
    fragNormal = normalMatrix * normal;
    gl_Position = modelViewProjection * vec4(position, 1.0);
    flogz = 1.0 + gl_Position.w;
}
)";

const char* kTrapVert = R"(#version 330 core
layout(location = 0) in vec3 unitPos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 tb;     // yb, yt, xbl, xbr
layout(location = 3) in vec2 tt;     // xtl, xtr
uniform mat4 modelViewProjection;
uniform mat3 normalMatrix;
uniform float zBottom;
uniform float thickness;
out vec3 fragNormal;
out float flogz;
void main() {
    float y = mix(tb.x, tb.y, unitPos.y);
    float xl = mix(tb.z, tt.x, unitPos.y);
    float xr = mix(tb.w, tt.y, unitPos.y);
    vec3 position = vec3(mix(xl, xr, unitPos.x), y, zBottom + unitPos.z * thickness);
    fragNormal = normalMatrix * normal;
    gl_Position = modelViewProjection * vec4(position, 1.0);
    flogz = 1.0 + gl_Position.w;
}
)";

const char* kShapeFrag = R"(#version 330 core
in vec3 fragNormal;
in float flogz;
uniform vec4 objectColor;
uniform vec3 lightDirection;
uniform bool selected;
uniform float Fcoef_half;
out vec4 FragColor;
void main() {
    vec3 norm = normalize(fragNormal);
    float lighting = 0.3 + max(dot(norm, -lightDirection), 0.0) * 0.6;
    vec3 color = objectColor.rgb * lighting;
    if (selected) {
        color = mix(color, vec3(1.0, 0.8, 0.0), 0.3);
    }
    FragColor = vec4(color, objectColor.a);
    gl_FragDepth = log2(max(1e-6, flogz)) * Fcoef_half;
}
)";

const char* kCoverVert = R"(#version 330 core
layout(location = 0) in vec2 corner;
layout(location = 1) in vec4 rect;   // x0, y0, x1, y1
layout(location = 2) in vec3 cell;   // atlas column, atlas row, page
uniform mat4 modelViewProjection;
uniform mat3 normalMatrix;
uniform float zTop;
uniform float cellSide;
uniform float atlasSide;
out vec3 uv;
out vec3 fragNormal;
out float flogz;
void main() {
    vec3 position = vec3(mix(rect.x, rect.z, corner.x), mix(rect.y, rect.w, corner.y), zTop);
    uv = vec3((cell.x * cellSide + mix(0.5, cellSide - 0.5, corner.x)) / atlasSide,
              (cell.y * cellSide + mix(0.5, cellSide - 0.5, corner.y)) / atlasSide,
              cell.z);
    fragNormal = normalMatrix * vec3(0.0, 0.0, 1.0);
    gl_Position = modelViewProjection * vec4(position, 1.0);
    flogz = 1.0 + gl_Position.w;
}
)";

const char* kCoverFrag = R"(#version 330 core
in vec3 uv;
in vec3 fragNormal;
in float flogz;
uniform sampler2DArray atlas;
uniform vec4 objectColor;
uniform vec3 lightDirection;
uniform bool selected;
uniform bool hatched;
uniform float Fcoef_half;
uniform float depthBias;
out vec4 FragColor;
void main() {
    float v = texture(atlas, uv).r;
    if (v <= 0.0) discard;
    float lighting = 0.3 + max(dot(normalize(fragNormal), -lightDirection), 0.0) * 0.6;
    vec3 color = objectColor.rgb * lighting * (0.3 + 0.7 * v);
    // Fill: diagonal bands 4 px wide on screen, every other one darker.
    if (hatched && mod(floor((gl_FragCoord.x + gl_FragCoord.y) / 4.0), 2.0) >= 1.0) {
        color *= 0.5;
    }
    if (selected) {
        color = mix(color, vec3(1.0, 0.8, 0.0), 0.3);
    }
    FragColor = vec4(color, objectColor.a);
    gl_FragDepth = log2(max(1e-6, flogz)) * Fcoef_half + depthBias;
}
)";

bool build(ShaderProgram& p, const char* vert, const char* frag)
{
    if (p.loadFromSource(vert, frag)) return true;
    qWarning() << "FastShapesGL: a shader did not build";
    return false;
}

// A unit prism, (0,0,0) to (1,1,1), outward faces counter-clockwise.
void unitPrism(std::vector<float>& v, std::vector<unsigned>& idx)
{
    const float f[6][4][3] = {
        {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},
        {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}},
        {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
        {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}},
        {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
        {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}},
    };
    const float n[6][3] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}};
    for (int i = 0; i < 6; ++i) {
        for (int k = 0; k < 4; ++k) {
            v.insert(v.end(), {f[i][k][0], f[i][k][1], f[i][k][2], n[i][0], n[i][1], n[i][2]});
        }
        const unsigned b = unsigned(i) * 4;
        idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    }
}

constexpr int kPrismIndices = 36;

}  // namespace

bool FastShapesGL::init(QOpenGLExtraFunctions* gl)
{
    if (m_initialized) return true;
    if (!build(m_boxProgram, kBoxVert, kShapeFrag) || !build(m_trapProgram, kTrapVert, kShapeFrag)
        || !build(m_coverProgram, kCoverVert, kCoverFrag)) {
        return false;
    }

    std::vector<float> v;
    std::vector<unsigned> idx;
    unitPrism(v, idx);
    gl->glGenBuffers(1, &m_prismVbo);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_prismVbo);
    gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(float)), v.data(),
                     GL_STATIC_DRAW);
    gl->glGenBuffers(1, &m_prismIbo);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_prismIbo);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * sizeof(unsigned)),
                     idx.data(), GL_STATIC_DRAW);

    const float quad[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    const unsigned quadIdx[6] = {0, 1, 2, 0, 2, 3};
    gl->glGenVertexArrays(1, &m_coverVao);
    gl->glBindVertexArray(m_coverVao);
    gl->glGenBuffers(1, &m_quadVbo);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    gl->glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    gl->glGenBuffers(1, &m_quadIbo);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_quadIbo);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIdx), quadIdx, GL_STATIC_DRAW);
    gl->glGenBuffers(1, &m_coverVbo);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_coverVbo);
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribDivisor(1, 1);
    gl->glEnableVertexAttribArray(2);
    gl->glVertexAttribDivisor(2, 1);
    gl->glBindVertexArray(0);
    gl->glGenQueries(1, &m_query);
    m_initialized = true;
    return true;
}

void FastShapesGL::prismAttributes(QOpenGLExtraFunctions* gl)
{
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_prismVbo);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_prismIbo);
}

void FastShapesGL::freeLayers(QOpenGLExtraFunctions* gl)
{
    for (auto& [layer, g] : m_layers) {
        const GLuint vaos[2] = {g.boxVao, g.trapVao};
        const GLuint vbos[2] = {g.boxVbo, g.trapVbo};
        gl->glDeleteVertexArrays(2, vaos);
        gl->glDeleteBuffers(2, vbos);
    }
    m_layers.clear();
    if (m_atlas) gl->glDeleteTextures(1, &m_atlas);
    m_atlas = 0;
}

void FastShapesGL::upload(QOpenGLExtraFunctions* gl, const std::vector<const FastLayer*>& layers)
{
    freeLayers(gl);
    std::size_t tiles = 0;
    for (const FastLayer* layer : layers) {
        LayerGL g;
        g.tileBase = tiles;
        tiles += layer->tiles.tiles.size();
        const TiledLayer& t = layer->tiles;
        if (!t.boxes.empty()) {
            gl->glGenVertexArrays(1, &g.boxVao);
            gl->glBindVertexArray(g.boxVao);
            prismAttributes(gl);
            gl->glGenBuffers(1, &g.boxVbo);
            gl->glBindBuffer(GL_ARRAY_BUFFER, g.boxVbo);
            gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(t.boxes.size() * sizeof(TileBox)),
                             t.boxes.data(), GL_STATIC_DRAW);
            gl->glEnableVertexAttribArray(2);
            gl->glVertexAttribDivisor(2, 1);
        }
        if (!t.traps.empty()) {
            gl->glGenVertexArrays(1, &g.trapVao);
            gl->glBindVertexArray(g.trapVao);
            prismAttributes(gl);
            gl->glGenBuffers(1, &g.trapVbo);
            gl->glBindBuffer(GL_ARRAY_BUFFER, g.trapVbo);
            gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(t.traps.size() * sizeof(TileTrap)),
                             t.traps.data(), GL_STATIC_DRAW);
            gl->glEnableVertexAttribArray(2);
            gl->glVertexAttribDivisor(2, 1);
            gl->glEnableVertexAttribArray(3);
            gl->glVertexAttribDivisor(3, 1);
        }
        m_layers.emplace(layer, g);
    }
    gl->glBindVertexArray(0);

    const std::size_t pages = (tiles + kTilesPerPage - 1) / kTilesPerPage;
    if (pages == 0) return;
    const std::size_t pageTexels = std::size_t(kAtlasSide) * kAtlasSide;
    std::vector<uint8_t> atlas(pageTexels * pages, 0);
    for (const FastLayer* layer : layers) {
        const TiledLayer& t = layer->tiles;
        const std::size_t base = m_layers.at(layer).tileBase;
        for (std::size_t k = 0; k < t.tiles.size(); ++k) {
            const std::size_t g = base + k;
            const std::size_t page = g / kTilesPerPage, inPage = g % kTilesPerPage;
            const std::size_t cx = inPage % kAtlasTiles, cy = inPage / kAtlasTiles;
            const uint8_t* src = t.map(k);
            for (int y = 0; y < kTileMapSide; ++y) {
                std::copy_n(src + std::size_t(y) * kTileMapSide, kTileMapSide,
                            atlas.data() + page * pageTexels
                                + (cy * kTileMapSide + std::size_t(y)) * kAtlasSide
                                + cx * kTileMapSide);
            }
        }
    }
    gl->glGenTextures(1, &m_atlas);
    gl->glBindTexture(GL_TEXTURE_2D_ARRAY, m_atlas);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, kAtlasSide, kAtlasSide, GLsizei(pages), 0,
                     GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    gl->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void FastShapesGL::release(QOpenGLExtraFunctions* gl)
{
    if (!m_initialized) return;
    freeLayers(gl);
    const GLuint buffers[5] = {m_prismVbo, m_prismIbo, m_quadVbo, m_quadIbo, m_coverVbo};
    gl->glDeleteBuffers(5, buffers);
    gl->glDeleteVertexArrays(1, &m_coverVao);
    gl->glDeleteQueries(1, &m_query);
    m_prismVbo = m_prismIbo = m_quadVbo = m_quadIbo = m_coverVbo = m_coverVao = m_query = 0;
    m_boxProgram.destroy();
    m_trapProgram.destroy();
    m_coverProgram.destroy();
    m_initialized = false;
}

void FastShapesGL::draw(QOpenGLExtraFunctions* gl, const std::vector<FastDrawLayer>& layers,
                        const TileChoice& choice, const FastFrameMatrices& m, bool translucent)
{
    if (!m_initialized) return;
    const QVector3D light = lightInView(m.view, m.lightDirection);
    auto wanted = [translucent](const FastDrawLayer& d) {
        return (d.color.w() < 1.0f) == translucent;
    };
    auto uniforms = [&](ShaderProgram& p, const FastDrawLayer& d) {
        const QMatrix4x4 modelView = m.view * d.model;
        p.setUniformMat4("modelViewProjection", m.projection * modelView);
        p.setUniformMat3("normalMatrix", modelView.normalMatrix());
        p.setUniformVec4("objectColor", d.color);
        p.setUniformBool("selected", d.selected);
        gl->glFrontFace(d.mirrored ? GL_CW : GL_CCW);
    };

    // Shapes: each layer's shape tiles as runs of instances, boxes then
    // trapezoids.
    for (int pass = 0; pass < 2; ++pass) {
        ShaderProgram& p = pass == 0 ? m_boxProgram : m_trapProgram;
        p.bind();
        p.setUniformVec3("lightDirection", light);
        p.setUniformFloat("Fcoef_half", m.fcoefHalf);
        for (const FastDrawLayer& d : layers) {
            if (!wanted(d)) continue;
            auto it = m_layers.find(d.layer);
            if (it == m_layers.end()) continue;
            const LayerGL& g = it->second;
            const GLuint vao = pass == 0 ? g.boxVao : g.trapVao;
            if (!vao) continue;
            const std::vector<TileBucket>& buckets = choice.buckets[d.choiceIndex];
            const TiledLayer& t = d.layer->tiles;
            bool bound = false;
            std::size_t r0 = 0, r1 = 0;
            auto flush = [&]() {
                if (r1 <= r0) return;
                if (!bound) {
                    uniforms(p, d);
                    p.setUniformFloat("zBottom", float(d.layer->place.zBottom));
                    p.setUniformFloat("thickness", float(d.layer->place.thickness));
                    gl->glBindVertexArray(vao);
                    gl->glBindBuffer(GL_ARRAY_BUFFER, pass == 0 ? g.boxVbo : g.trapVbo);
                    bound = true;
                }
                if (pass == 0) {
                    gl->glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TileBox),
                                              reinterpret_cast<void*>(r0 * sizeof(TileBox)));
                } else {
                    gl->glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TileTrap),
                                              reinterpret_cast<void*>(r0 * sizeof(TileTrap)));
                    gl->glVertexAttribPointer(
                        3, 2, GL_FLOAT, GL_FALSE, sizeof(TileTrap),
                        reinterpret_cast<void*>(r0 * sizeof(TileTrap) + 4 * sizeof(float)));
                }
                gl->glDrawElementsInstanced(GL_TRIANGLES, kPrismIndices, GL_UNSIGNED_INT, nullptr,
                                            GLsizei(r1 - r0));
            };
            for (std::size_t k = 0; k < t.tiles.size(); ++k) {
                if (buckets[k] != TileBucket::Shape) continue;
                const ShapeTile& tile = t.tiles[k];
                const std::size_t a = pass == 0 ? tile.b0 : tile.t0;
                const std::size_t b = pass == 0 ? tile.b1 : tile.t1;
                if (a == b) continue;
                if (a == r1) {
                    r1 = b;
                    continue;
                }
                flush();
                r0 = a;
                r1 = b;
            }
            flush();
        }
        p.release();
    }

    // Coverage: one quad per coverage tile, every layer in one buffer.
    m_coverInstances.clear();
    std::vector<std::pair<std::size_t, std::size_t>> ranges(layers.size(), {0, 0});
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const FastDrawLayer& d = layers[i];
        if (!wanted(d)) continue;
        auto it = m_layers.find(d.layer);
        if (it == m_layers.end()) continue;
        const std::vector<TileBucket>& buckets = choice.buckets[d.choiceIndex];
        const TiledLayer& t = d.layer->tiles;
        ranges[i].first = m_coverInstances.size() / kCoverFloats;
        for (std::size_t k = 0; k < t.tiles.size(); ++k) {
            if (buckets[k] != TileBucket::Coverage) continue;
            const ShapeTile& tile = t.tiles[k];
            const std::size_t g = it->second.tileBase + k;
            const std::size_t inPage = g % kTilesPerPage;
            m_coverInstances.insert(m_coverInstances.end(),
                                    {tile.x0, tile.y0, tile.x1, tile.y1,
                                     float(inPage % kAtlasTiles), float(inPage / kAtlasTiles),
                                     float(g / kTilesPerPage)});
        }
        ranges[i].second = m_coverInstances.size() / kCoverFloats;
    }
    if (!m_coverInstances.empty()) {
        ShaderProgram& p = m_coverProgram;
        p.bind();
        p.setUniformVec3("lightDirection", light);
        p.setUniformFloat("Fcoef_half", m.fcoefHalf);
        p.setUniformFloat("cellSide", float(kTileMapSide));
        p.setUniformFloat("atlasSide", float(kAtlasSide));
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D_ARRAY, m_atlas);
        gl->glBindVertexArray(m_coverVao);
        gl->glBindBuffer(GL_ARRAY_BUFFER, m_coverVbo);
        gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(m_coverInstances.size() * sizeof(float)),
                         m_coverInstances.data(), GL_STREAM_DRAW);
        const GLsizei stride = GLsizei(kCoverFloats * sizeof(float));
        for (std::size_t i = 0; i < layers.size(); ++i) {
            const auto [first, end] = ranges[i];
            if (end <= first) continue;
            const FastDrawLayer& d = layers[i];
            uniforms(p, d);
            p.setUniformFloat("zTop", float(d.layer->place.zBottom + d.layer->place.thickness));
            p.setUniformFloat("depthBias", kCoverageDepthStep * float(1 + d.coverRank));
            p.setUniformBool("hatched", d.layer->place.fill);
            const std::size_t at = first * kCoverFloats * sizeof(float);
            gl->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                                      reinterpret_cast<void*>(at));
            gl->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride,
                                      reinterpret_cast<void*>(at + 4 * sizeof(float)));
            gl->glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr,
                                        GLsizei(end - first));
        }
        p.release();
        gl->glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }
    gl->glBindVertexArray(0);
    gl->glFrontFace(GL_CCW);
}

void FastShapesGL::beginCount(QOpenGLExtraFunctions* gl)
{
    gl->glBeginQuery(GL_PRIMITIVES_GENERATED, m_query);
}

std::size_t FastShapesGL::endCount(QOpenGLExtraFunctions* gl)
{
    gl->glEndQuery(GL_PRIMITIVES_GENERATED);
    GLuint n = 0;
    gl->glGetQueryObjectuiv(m_query, GL_QUERY_RESULT, &n);
    return n;
}

} // namespace chiplet
