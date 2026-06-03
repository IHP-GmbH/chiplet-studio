// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * AssemblyRenderer.cpp - Implementation
 */

#include "AssemblyRenderer.h"

namespace chiplet {

AssemblyRenderer::AssemblyRenderer() = default;
AssemblyRenderer::~AssemblyRenderer() = default;

void AssemblyRenderer::setAssembly(Assembly* assembly)
{
    m_assembly = assembly;
}

void AssemblyRenderer::render()
{
    // TODO: Implement OpenGL rendering
}

} // namespace chiplet
