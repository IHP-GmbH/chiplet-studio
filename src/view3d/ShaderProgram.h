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

    // Shader lifecycle
    void bind();
    void release();
    // Delete the GL program. MUST be called with the owning GL context current
    // (e.g. inside makeCurrent()/doneCurrent()). After this the destructor is a
    // no-op, which avoids glDeleteProgram running with no current context when
    // the owning widget is torn down.
    void destroy();
    bool isValid() const { return m_programId != 0; }

    // Uniform setters
    void setUniformMat4(const QString& name, const QMatrix4x4& matrix);
    void setUniformMat3(const QString& name, const QMatrix3x3& matrix);
    void setUniformVec3(const QString& name, const QVector3D& vec);
    void setUniformVec4(const QString& name, const QVector4D& vec);
    void setUniformFloat(const QString& name, float value);
    void setUniformBool(const QString& name, bool value);

    // Get uniform location (cached)
    int uniformLocation(const QString& name);

    GLuint programId() const { return m_programId; }

private:
    GLuint compileShader(GLenum type, const QString& source);
    bool linkProgram(GLuint vertShader, GLuint fragShader);

    GLuint m_programId = 0;
    QMap<QString, int> m_uniformCache;
    bool m_initialized = false;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_SHADERPROGRAM_H
