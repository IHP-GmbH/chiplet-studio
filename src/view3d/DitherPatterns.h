// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * DitherPatterns.h - KLayout dither pattern texture generator
 *
 * Generates OpenGL textures for KLayout pattern IDs (C0-C47, I1-I16).
 * Patterns are 16x16 bitmaps tiled across surfaces.
 */

#ifndef CHIPLET_VIEW3D_DITHERPATTERNS_H
#define CHIPLET_VIEW3D_DITHERPATTERNS_H

#include <QOpenGLFunctions>
#include <QString>
#include <array>
#include <map>
#include <cstdint>

namespace chiplet {

/**
 * 16x16 pattern bitmap (256 bits = 32 bytes)
 * Each row is 16 bits (stored in 2 bytes).
 */
struct PatternBitmap {
    std::array<uint8_t, 32> data{};  // 16 rows * 2 bytes each

    // Set a pixel (x, y in 0-15 range)
    void setPixel(int x, int y, bool on);

    // Get a pixel
    bool getPixel(int x, int y) const;

    // Fill entire pattern
    void fill(bool on);

    // Create checkerboard with given spacing
    static PatternBitmap checkerboard(int spacing);

    // Create horizontal lines with given spacing
    static PatternBitmap horizontalLines(int spacing);

    // Create vertical lines with given spacing
    static PatternBitmap verticalLines(int spacing);

    // Create diagonal lines (left-to-right)
    static PatternBitmap diagonalLinesLR(int spacing);

    // Create diagonal lines (right-to-left)
    static PatternBitmap diagonalLinesRL(int spacing);

    // Create dot grid with given spacing
    static PatternBitmap dots(int spacingX, int spacingY);

    // Create sparse stipple
    static PatternBitmap sparse();

    // Create dense stipple
    static PatternBitmap dense();
};

/**
 * DitherPatterns manages KLayout-compatible pattern textures.
 */
class DitherPatterns : protected QOpenGLFunctions {
public:
    static constexpr int PATTERN_SIZE = 16;  // 16x16 pixels

    DitherPatterns();
    ~DitherPatterns();

    // Non-copyable
    DitherPatterns(const DitherPatterns&) = delete;
    DitherPatterns& operator=(const DitherPatterns&) = delete;

    // Initialize OpenGL resources (call after context is current)
    void initialize();

    // Clean up OpenGL resources
    void cleanup();

    // Get OpenGL texture ID for a pattern
    // Returns 0 if pattern is solid (C0) or unknown
    GLuint getPatternTexture(const QString& patternId);

    // Check if pattern is solid (no texture needed)
    static bool isSolid(const QString& patternId);

    // Check if pattern exists
    bool hasPattern(const QString& patternId) const;

    // Pre-generate all standard patterns
    void initializeStandardPatterns();

private:
    // Generate texture from bitmap
    GLuint createTextureFromBitmap(const PatternBitmap& bitmap);

    // Generate pattern bitmap for given ID
    PatternBitmap generatePatternBitmap(const QString& patternId);

    // Cached textures: patternId -> GL texture ID
    std::map<QString, GLuint> m_textures;

    // Pre-generated pattern bitmaps
    std::map<QString, PatternBitmap> m_bitmaps;

    bool m_initialized = false;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_DITHERPATTERNS_H
