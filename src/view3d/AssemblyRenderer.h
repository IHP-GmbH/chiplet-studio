// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * AssemblyRenderer.h - OpenGL renderer for 3D assembly view
 */

#ifndef CHIPLET_VIEW3D_ASSEMBLYRENDERER_H
#define CHIPLET_VIEW3D_ASSEMBLYRENDERER_H

#include "core/Assembly.h"

namespace chiplet {

/**
 * AssemblyRenderer handles 3D visualization of chiplet assemblies.
 */
class AssemblyRenderer {
public:
    AssemblyRenderer();
    ~AssemblyRenderer();

    void setAssembly(Assembly* assembly);
    void render();

private:
    Assembly* m_assembly = nullptr;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_ASSEMBLYRENDERER_H
