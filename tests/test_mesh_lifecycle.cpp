// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QSurfaceFormat>
#include <utility>
#include "view3d/MeshBuilder.h"

namespace chiplet {
namespace {

class MeshLifecycle : public ::testing::Test {
protected:
    QOpenGLContext context;
    QOffscreenSurface surface;
    QOpenGLExtraFunctions* gl = nullptr;

    void SetUp() override {
        QSurfaceFormat format;
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        context.setFormat(format);
        ASSERT_TRUE(context.create()) << "Native OpenGL context is required, not skipped";
        surface.setFormat(context.format());
        surface.create();
        ASSERT_TRUE(surface.isValid());
        ASSERT_TRUE(context.makeCurrent(&surface));
        const auto actual = context.format();
        ASSERT_TRUE(actual.majorVersion() > 3 ||
                    (actual.majorVersion() == 3 && actual.minorVersion() >= 3));
        gl = context.extraFunctions();
        ASSERT_NE(gl, nullptr);
        gl->initializeOpenGLFunctions();
    }

    void TearDown() override { context.doneCurrent(); }

    struct Handles { GLuint vao, vbo, ebo; };

    Handles inspect(const ComponentMesh& mesh) {
        GLint vbo = 0, ebo = 0;
        const auto vao = mesh.vao();
        EXPECT_NE(vao, 0u);
        EXPECT_TRUE(gl->glIsVertexArray(vao));
        gl->glBindVertexArray(vao);
        gl->glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &vbo);
        gl->glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebo);
        gl->glBindVertexArray(0);
        EXPECT_NE(vbo, 0);
        EXPECT_NE(ebo, 0);
        EXPECT_TRUE(gl->glIsBuffer(static_cast<GLuint>(vbo)));
        EXPECT_TRUE(gl->glIsBuffer(static_cast<GLuint>(ebo)));
        EXPECT_EQ(gl->glGetError(), GLenum(GL_NO_ERROR));
        return {vao, static_cast<GLuint>(vbo), static_cast<GLuint>(ebo)};
    }

    void expectDeleted(const Handles& handles) {
        EXPECT_FALSE(gl->glIsVertexArray(handles.vao));
        EXPECT_FALSE(gl->glIsBuffer(handles.vbo));
        EXPECT_FALSE(gl->glIsBuffer(handles.ebo));
        EXPECT_EQ(gl->glGetError(), GLenum(GL_NO_ERROR));
    }
};

TEST_F(MeshLifecycle, UploadMoveReleaseBeforeRender) {
    auto source = MeshBuilder::buildBox(1, 1, 1);
    source.upload();
    ASSERT_TRUE(source.isUploaded());
    const auto handles = inspect(source);
    ComponentMesh moved(std::move(source));
    EXPECT_FALSE(source.isUploaded());
    EXPECT_EQ(moved.vao(), handles.vao);
    source.release();
    EXPECT_TRUE(gl->glIsVertexArray(handles.vao));
    // No render/upload of the destination: deletion must initialize dispatch.
    moved.release();
    EXPECT_FALSE(moved.isUploaded());
    expectDeleted(handles);
    moved.release(); // idempotent; destructors also run before context teardown
}

TEST_F(MeshLifecycle, MoveAssignmentReleasesUnrenderedDestination) {
    auto seed = MeshBuilder::buildBox(1, 1, 1);
    seed.upload();
    ASSERT_TRUE(seed.isUploaded());
    const auto oldHandles = inspect(seed);
    ComponentMesh destination(std::move(seed));
    auto source = MeshBuilder::buildBox(2, 2, 2);
    source.upload();
    ASSERT_TRUE(source.isUploaded());
    const auto newHandles = inspect(source);
    ASSERT_NE(oldHandles.vao, newHandles.vao);
    destination = std::move(source); // releases moved, never-rendered old target
    expectDeleted(oldHandles);
    EXPECT_FALSE(source.isUploaded());
    EXPECT_EQ(destination.vao(), newHandles.vao);
    EXPECT_TRUE(gl->glIsVertexArray(newHandles.vao));
    EXPECT_TRUE(gl->glIsBuffer(newHandles.vbo));
    EXPECT_TRUE(gl->glIsBuffer(newHandles.ebo));
    destination.release();
    expectDeleted(newHandles);
}

} // namespace
} // namespace chiplet
