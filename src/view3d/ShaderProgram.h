// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ShaderProgram.h - OpenGL shader program wrapper
 */

#ifndef CHIPLET_VIEW3D_SHADERPROGRAM_H
#define CHIPLET_VIEW3D_SHADERPROGRAM_H

#include <QOpenGLFunctions>
#include <QString>
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>
#include <QColor>

namespace chiplet {

/**
 * ShaderProgram wraps OpenGL shader compilation and uniform handling.
 */
class ShaderProgram : protected QOpenGLFunctions {
public:
    ShaderProgram();
    ~ShaderProgram();

    // Non-copyable
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    // Load shaders from source strings
    bool loadFromSource(const QString& vertexSource, const QString& fragmentSource);

    // Load shaders from Qt resource files
    bool loadFromResource(const QString& vertexPath, const QString& fragmentPath);

    // Shader lifecycle
    void bind();
    void release();
    bool isValid() const { return m_programId != 0; }

    // Uniform setters
    void setUniformMat4(const QString& name, const QMatrix4x4& matrix);
    void setUniformMat3(const QString& name, const QMatrix3x3& matrix);
    void setUniformVec3(const QString& name, const QVector3D& vec);
    void setUniformVec4(const QString& name, const QVector4D& vec);
    void setUniformFloat(const QString& name, float value);
    void setUniformInt(const QString& name, int value);
    void setUniformBool(const QString& name, bool value);
    void setUniformColor(const QString& name, const QColor& color);

    // Get uniform location (cached)
    int uniformLocation(const QString& name);

    // Get attribute location
    int attributeLocation(const QString& name);

    GLuint programId() const { return m_programId; }

private:
    GLuint compileShader(GLenum type, const QString& source);
    bool linkProgram(GLuint vertShader, GLuint fragShader);
    QString readResource(const QString& path);

    GLuint m_programId = 0;
    QMap<QString, int> m_uniformCache;
    bool m_initialized = false;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_SHADERPROGRAM_H
