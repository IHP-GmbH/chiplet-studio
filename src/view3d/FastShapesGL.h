// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * FastShapesGL.h - The fast view's shapes and coverage, in GL
 *
 * Every prepared record is uploaded once: boxes and trapezoids as instance
 * data for one unit prism (12 triangles each), and every tile's coverage map
 * into a texture array. A frame draws, per layer, the tiles the choice made
 * shapes as instance ranges, and the tiles it made coverage as one textured
 * quad each (2 triangles) on the layer's top face. A quad's depth is pushed
 * back a few steps of the depth buffer, so a shape on the same face wins,
 * and layers that share a top (a drawing layer and its fill) are pushed back
 * by different steps, so their quads do not fight. A fill layer's quads are
 * hatched, so fill reads apart from the metal it shares a colour with.
 *
 * Needs a current 3.3 core context for every call but initialized() and has().
 */

#ifndef CHIPLET_VIEW3D_FASTSHAPESGL_H
#define CHIPLET_VIEW3D_FASTSHAPESGL_H

#include "SceneLoad.h"
#include "ShaderProgram.h"
#include "TileChoice.h"

#include <QMatrix4x4>
#include <QOpenGLExtraFunctions>
#include <QVector3D>
#include <QVector4D>

#include <cstddef>
#include <map>
#include <vector>

namespace chiplet {

// How far a coverage quad's depth is pushed back per step, in the units of
// gl_FragDepth: a few steps of a 24-bit depth buffer.
inline constexpr float kCoverageDepthStep = 1.0f / float(1 << 22);

// One fast layer as this frame draws it.
struct FastDrawLayer {
    const FastLayer* layer = nullptr;
    std::size_t choiceIndex = 0;  // its entry in TileChoice::layers and ::buckets
    QMatrix4x4 model;             // GDS um -> world, with the layer Z spread
    QVector4D color;              // alpha < 1 draws in the translucent pass
    bool selected = false;
    bool mirrored = false;        // face down: the winding is reversed
    int coverRank = 0;            // order among its component's layers with the same top
};

struct FastFrameMatrices {
    QMatrix4x4 view;
    QMatrix4x4 projection;
    QVector3D lightDirection;  // the scene's, in world space; draw turns it by view
    float fcoefHalf = 0.0f;
};

class FastShapesGL {
public:
    FastShapesGL() = default;
    FastShapesGL(const FastShapesGL&) = delete;
    FastShapesGL& operator=(const FastShapesGL&) = delete;

    // Compile the programs and the shared meshes. False if a program does
    // not build.
    bool init(QOpenGLExtraFunctions* gl);

    // Replace what is uploaded with these layers' records and maps.
    void upload(QOpenGLExtraFunctions* gl, const std::vector<const FastLayer*>& layers);

    // Free every GL object, init's included.
    void release(QOpenGLExtraFunctions* gl);

    bool initialized() const { return m_initialized; }
    bool has(const FastLayer* layer) const { return m_layers.count(layer) != 0; }

    // Draw the layers whose alpha is 1 (translucent false) or below 1
    // (translucent true). Depth and cull state are the caller's.
    void draw(QOpenGLExtraFunctions* gl, const std::vector<FastDrawLayer>& layers,
              const TileChoice& choice, const FastFrameMatrices& m, bool translucent);

    // Triangles drawn between begin and end, from a GL_PRIMITIVES_GENERATED
    // query.
    void beginCount(QOpenGLExtraFunctions* gl);
    std::size_t endCount(QOpenGLExtraFunctions* gl);

private:
    struct LayerGL {
        unsigned int boxVao = 0, boxVbo = 0;
        unsigned int trapVao = 0, trapVbo = 0;
        std::size_t tileBase = 0;  // first tile's index in the atlas
    };

    void prismAttributes(QOpenGLExtraFunctions* gl);
    void freeLayers(QOpenGLExtraFunctions* gl);

    bool m_initialized = false;
    ShaderProgram m_boxProgram, m_trapProgram, m_coverProgram;
    unsigned int m_prismVbo = 0, m_prismIbo = 0;
    unsigned int m_quadVbo = 0, m_quadIbo = 0, m_coverVao = 0, m_coverVbo = 0;
    unsigned int m_atlas = 0, m_query = 0;
    std::map<const FastLayer*, LayerGL> m_layers;
    std::vector<float> m_coverInstances;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_FASTSHAPESGL_H
