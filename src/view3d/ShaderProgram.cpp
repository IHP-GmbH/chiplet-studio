// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ShaderProgram.cpp - OpenGL shader program implementation
 */

#include "ShaderProgram.h"
#include <QFile>
#include <QDebug>

namespace chiplet {

ShaderProgram::ShaderProgram()
{
}

ShaderProgram::~ShaderProgram()
{
    if (m_programId != 0 && m_initialized) {
        glDeleteProgram(m_programId);
    }
}

bool ShaderProgram::loadFromSource(const QString& vertexSource, const QString& fragmentSource)
{
    if (!m_initialized) {
        initializeOpenGLFunctions();
        m_initialized = true;
    }

    // Delete existing program if any
    if (m_programId != 0) {
        glDeleteProgram(m_programId);
        m_programId = 0;
        m_uniformCache.clear();
    }

    GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    if (vertShader == 0) {
        return false;
    }

    GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (fragShader == 0) {
        glDeleteShader(vertShader);
        return false;
    }

    bool success = linkProgram(vertShader, fragShader);

    // Shaders can be deleted after linking
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    return success;
}

bool ShaderProgram::loadFromResource(const QString& vertexPath, const QString& fragmentPath)
{
    QString vertexSource = readResource(vertexPath);
    QString fragmentSource = readResource(fragmentPath);

    if (vertexSource.isEmpty() || fragmentSource.isEmpty()) {
        return false;
    }

    return loadFromSource(vertexSource, fragmentSource);
}

void ShaderProgram::bind()
{
    if (m_programId != 0) {
        glUseProgram(m_programId);
    }
}

void ShaderProgram::release()
{
    glUseProgram(0);
}

void ShaderProgram::setUniformMat4(const QString& name, const QMatrix4x4& matrix)
{
    int loc = uniformLocation(name);
    if (loc >= 0) {
        glUniformMatrix4fv(loc, 1, GL_FALSE, matrix.constData());
    }
}

void ShaderProgram::setUniformMat3(const QString& name, const QMatrix3x3& matrix)
{
    int loc = uniformLocation(name);
    if (loc >= 0) {
        glUniformMatrix3fv(loc, 1, GL_FALSE, matrix.constData());
    }
}

void ShaderProgram::setUniformVec3(const QString& name, const QVector3D& vec)
{
    int loc = uniformLocation(name);
    if (loc >= 0) {
        glUniform3f(loc, vec.x(), vec.y(), vec.z());
    }
}

void ShaderProgram::setUniformVec4(const QString& name, const QVector4D& vec)
{
    int loc = uniformLocation(name);
    if (loc >= 0) {
        glUniform4f(loc, vec.x(), vec.y(), vec.z(), vec.w());
    }
}

void ShaderProgram::setUniformFloat(const QString& name, float value)
{
    int loc = uniformLocation(name);
    if (loc >= 0) {
        glUniform1f(loc, value);
    }
}

void ShaderProgram::setUniformInt(const QString& name, int value)
{
    int loc = uniformLocation(name);
    if (loc >= 0) {
        glUniform1i(loc, value);
    }
}

void ShaderProgram::setUniformBool(const QString& name, bool value)
{
    int loc = uniformLocation(name);
    if (loc >= 0) {
        glUniform1i(loc, value ? 1 : 0);
    }
}

void ShaderProgram::setUniformColor(const QString& name, const QColor& color)
{
    int loc = uniformLocation(name);
    if (loc >= 0) {
        glUniform4f(loc, color.redF(), color.greenF(), color.blueF(), color.alphaF());
    }
}

int ShaderProgram::uniformLocation(const QString& name)
{
    if (m_uniformCache.contains(name)) {
        return m_uniformCache[name];
    }

    int loc = glGetUniformLocation(m_programId, name.toUtf8().constData());
    m_uniformCache[name] = loc;
    return loc;
}

int ShaderProgram::attributeLocation(const QString& name)
{
    return glGetAttribLocation(m_programId, name.toUtf8().constData());
}

GLuint ShaderProgram::compileShader(GLenum type, const QString& source)
{
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        qWarning() << "Failed to create shader";
        return 0;
    }

    QByteArray sourceBytes = source.toUtf8();
    const char* sourcePtr = sourceBytes.constData();
    glShaderSource(shader, 1, &sourcePtr, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            QByteArray infoLog(infoLen, 0);
            glGetShaderInfoLog(shader, infoLen, nullptr, infoLog.data());
            qWarning() << "Shader compilation error:"
                       << (type == GL_VERTEX_SHADER ? "vertex" : "fragment")
                       << "\n" << infoLog;
        }
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

bool ShaderProgram::linkProgram(GLuint vertShader, GLuint fragShader)
{
    m_programId = glCreateProgram();
    if (m_programId == 0) {
        qWarning() << "Failed to create program";
        return false;
    }

    glAttachShader(m_programId, vertShader);
    glAttachShader(m_programId, fragShader);
    glLinkProgram(m_programId);

    GLint linked = 0;
    glGetProgramiv(m_programId, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint infoLen = 0;
        glGetProgramiv(m_programId, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            QByteArray infoLog(infoLen, 0);
            glGetProgramInfoLog(m_programId, infoLen, nullptr, infoLog.data());
            qWarning() << "Program link error:\n" << infoLog;
        }
        glDeleteProgram(m_programId);
        m_programId = 0;
        return false;
    }

    return true;
}

QString ShaderProgram::readResource(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open shader resource:" << path;
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace chiplet
