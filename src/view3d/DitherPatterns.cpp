// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * DitherPatterns.cpp - KLayout dither pattern texture generator implementation
 */

#include "DitherPatterns.h"
#include <QDebug>

namespace chiplet {

// =============================================================================
// PatternBitmap implementation
// =============================================================================

void PatternBitmap::setPixel(int x, int y, bool on)
{
    if (x < 0 || x >= 16 || y < 0 || y >= 16) return;

    int byteIndex = y * 2 + (x / 8);
    int bitIndex = 7 - (x % 8);

    if (on) {
        data[byteIndex] |= (1 << bitIndex);
    } else {
        data[byteIndex] &= ~(1 << bitIndex);
    }
}

bool PatternBitmap::getPixel(int x, int y) const
{
    if (x < 0 || x >= 16 || y < 0 || y >= 16) return false;

    int byteIndex = y * 2 + (x / 8);
    int bitIndex = 7 - (x % 8);
    return (data[byteIndex] >> bitIndex) & 1;
}

void PatternBitmap::fill(bool on)
{
    uint8_t val = on ? 0xFF : 0x00;
    for (auto& b : data) {
        b = val;
    }
}

PatternBitmap PatternBitmap::checkerboard(int spacing)
{
    PatternBitmap bmp;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            bool on = ((x / spacing) + (y / spacing)) % 2 == 0;
            bmp.setPixel(x, y, on);
        }
    }
    return bmp;
}

PatternBitmap PatternBitmap::horizontalLines(int spacing)
{
    PatternBitmap bmp;
    for (int y = 0; y < 16; ++y) {
        bool rowOn = (y % spacing) == 0;
        for (int x = 0; x < 16; ++x) {
            bmp.setPixel(x, y, rowOn);
        }
    }
    return bmp;
}

PatternBitmap PatternBitmap::verticalLines(int spacing)
{
    PatternBitmap bmp;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            bool colOn = (x % spacing) == 0;
            bmp.setPixel(x, y, colOn);
        }
    }
    return bmp;
}

PatternBitmap PatternBitmap::diagonalLinesLR(int spacing)
{
    PatternBitmap bmp;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            bool on = ((x + y) % spacing) == 0;
            bmp.setPixel(x, y, on);
        }
    }
    return bmp;
}

PatternBitmap PatternBitmap::diagonalLinesRL(int spacing)
{
    PatternBitmap bmp;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            bool on = ((x - y + 16) % spacing) == 0;
            bmp.setPixel(x, y, on);
        }
    }
    return bmp;
}

PatternBitmap PatternBitmap::dots(int spacingX, int spacingY)
{
    PatternBitmap bmp;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            bool on = (x % spacingX == 0) && (y % spacingY == 0);
            bmp.setPixel(x, y, on);
        }
    }
    return bmp;
}

PatternBitmap PatternBitmap::sparse()
{
    // Sparse stipple: every 4th pixel
    PatternBitmap bmp;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            bool on = ((x + y * 2) % 4) == 0;
            bmp.setPixel(x, y, on);
        }
    }
    return bmp;
}

PatternBitmap PatternBitmap::dense()
{
    // Dense stipple: 3/4 pixels on
    PatternBitmap bmp;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            bool on = !((x + y * 2) % 4 == 0);
            bmp.setPixel(x, y, on);
        }
    }
    return bmp;
}

// =============================================================================
// DitherPatterns implementation
// =============================================================================

DitherPatterns::DitherPatterns()
{
}

DitherPatterns::~DitherPatterns()
{
    cleanup();
}

void DitherPatterns::initialize()
{
    if (m_initialized) return;

    initializeOpenGLFunctions();
    initializeStandardPatterns();
    m_initialized = true;
}

void DitherPatterns::cleanup()
{
    if (!m_initialized) return;

    for (auto& [id, texId] : m_textures) {
        if (texId != 0) {
            glDeleteTextures(1, &texId);
        }
    }
    m_textures.clear();
    m_bitmaps.clear();
    m_initialized = false;
}

bool DitherPatterns::isSolid(const QString& patternId)
{
    return patternId.isEmpty() || patternId == "C0";
}

bool DitherPatterns::hasPattern(const QString& patternId) const
{
    return m_bitmaps.find(patternId) != m_bitmaps.end();
}

GLuint DitherPatterns::getPatternTexture(const QString& patternId)
{
    // Solid patterns don't need a texture
    if (isSolid(patternId)) {
        return 0;
    }

    // Check cache
    auto it = m_textures.find(patternId);
    if (it != m_textures.end()) {
        return it->second;
    }

    // Generate pattern bitmap
    PatternBitmap bitmap = generatePatternBitmap(patternId);

    // Create texture
    GLuint texId = createTextureFromBitmap(bitmap);
    m_textures[patternId] = texId;

    return texId;
}

GLuint DitherPatterns::createTextureFromBitmap(const PatternBitmap& bitmap)
{
    // Convert bitmap to RGBA texture data
    std::array<uint8_t, PATTERN_SIZE * PATTERN_SIZE * 4> pixels;

    for (int y = 0; y < PATTERN_SIZE; ++y) {
        for (int x = 0; x < PATTERN_SIZE; ++x) {
            int idx = (y * PATTERN_SIZE + x) * 4;
            bool on = bitmap.getPixel(x, y);

            // White where pattern is on, transparent where off
            pixels[idx + 0] = 255;  // R
            pixels[idx + 1] = 255;  // G
            pixels[idx + 2] = 255;  // B
            pixels[idx + 3] = on ? 255 : 0;  // A
        }
    }

    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);

    // Set texture parameters for tiling
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Upload texture data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, PATTERN_SIZE, PATTERN_SIZE,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    glBindTexture(GL_TEXTURE_2D, 0);

    return texId;
}

PatternBitmap DitherPatterns::generatePatternBitmap(const QString& patternId)
{
    // Check pre-generated bitmaps
    auto it = m_bitmaps.find(patternId);
    if (it != m_bitmaps.end()) {
        return it->second;
    }

    // Default to checkerboard for unknown patterns
    qDebug() << "DitherPatterns: unknown pattern ID:" << patternId << ", using default";
    return PatternBitmap::checkerboard(2);
}

void DitherPatterns::initializeStandardPatterns()
{
    // KLayout standard patterns (C0-C15, I1-I16)
    // These approximate the standard KLayout patterns

    // C0: Solid (handled specially, no bitmap needed)

    // C1: Horizontal lines, spacing 2
    m_bitmaps["C1"] = PatternBitmap::horizontalLines(2);

    // C2: Vertical lines, spacing 2
    m_bitmaps["C2"] = PatternBitmap::verticalLines(2);

    // C3: Diagonal lines (LR), spacing 2
    m_bitmaps["C3"] = PatternBitmap::diagonalLinesLR(2);

    // C4: Diagonal lines (RL), spacing 2
    m_bitmaps["C4"] = PatternBitmap::diagonalLinesRL(2);

    // C5: Grid (horizontal + vertical), spacing 4
    {
        PatternBitmap grid;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                bool on = (x % 4 == 0) || (y % 4 == 0);
                grid.setPixel(x, y, on);
            }
        }
        m_bitmaps["C5"] = grid;
    }

    // C6: Crosshatch (both diagonals), spacing 4
    {
        PatternBitmap cross;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                bool on = ((x + y) % 4 == 0) || ((x - y + 16) % 4 == 0);
                cross.setPixel(x, y, on);
            }
        }
        m_bitmaps["C6"] = cross;
    }

    // C7: Dots, 4x4 spacing
    m_bitmaps["C7"] = PatternBitmap::dots(4, 4);

    // C8: Checkerboard, 1px
    m_bitmaps["C8"] = PatternBitmap::checkerboard(1);

    // C9: Checkerboard, 2px
    m_bitmaps["C9"] = PatternBitmap::checkerboard(2);

    // C10: Horizontal lines, spacing 4
    m_bitmaps["C10"] = PatternBitmap::horizontalLines(4);

    // C11: Vertical lines, spacing 4
    m_bitmaps["C11"] = PatternBitmap::verticalLines(4);

    // C12: Diagonal lines (LR), spacing 4
    m_bitmaps["C12"] = PatternBitmap::diagonalLinesLR(4);

    // C13: Diagonal lines (RL), spacing 4
    m_bitmaps["C13"] = PatternBitmap::diagonalLinesRL(4);

    // C14: Sparse stipple
    m_bitmaps["C14"] = PatternBitmap::sparse();

    // C15: Dense stipple
    m_bitmaps["C15"] = PatternBitmap::dense();

    // I1: Inverted checkerboard (used in sg13g2.lyp for Substrate)
    {
        PatternBitmap inv = PatternBitmap::checkerboard(2);
        // Invert all bits
        for (auto& b : inv.data) {
            b = ~b;
        }
        m_bitmaps["I1"] = inv;
    }

    // Additional patterns for C16-C31 (simplified versions)
    for (int i = 16; i <= 31; ++i) {
        QString id = QString("C%1").arg(i);
        // Use variations based on index
        int variant = (i - 16) % 8;
        switch (variant) {
            case 0: m_bitmaps[id] = PatternBitmap::horizontalLines(8); break;
            case 1: m_bitmaps[id] = PatternBitmap::verticalLines(8); break;
            case 2: m_bitmaps[id] = PatternBitmap::diagonalLinesLR(8); break;
            case 3: m_bitmaps[id] = PatternBitmap::diagonalLinesRL(8); break;
            case 4: m_bitmaps[id] = PatternBitmap::dots(2, 2); break;
            case 5: m_bitmaps[id] = PatternBitmap::dots(8, 8); break;
            case 6: m_bitmaps[id] = PatternBitmap::checkerboard(4); break;
            case 7: m_bitmaps[id] = PatternBitmap::checkerboard(8); break;
        }
    }

    // I2-I16: Inverted versions of C2-C16
    for (int i = 2; i <= 16; ++i) {
        QString srcId = QString("C%1").arg(i);
        QString dstId = QString("I%1").arg(i);

        if (m_bitmaps.find(srcId) != m_bitmaps.end()) {
            PatternBitmap inv = m_bitmaps[srcId];
            for (auto& b : inv.data) {
                b = ~b;
            }
            m_bitmaps[dstId] = inv;
        }
    }
}

} // namespace chiplet
