// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * LayerStackup.h - Layer stackup definition for 2.5D visualization
 *
 * Defines the Z-axis positions and thicknesses of GDS layers
 * for extruding 2D polygons into 3D geometry.
 */

#ifndef CHIPLET_CORE_LAYERSTACKUP_H
#define CHIPLET_CORE_LAYERSTACKUP_H

#include <map>
#include <vector>
#include <string>
#include "view2d/LayerProperties.h"

namespace chiplet {

/**
 * Layer elevation definition for 3D extrusion
 */
struct LayerElevation {
    int layer = 0;
    int datatype = 0;
    double z_bottom = 0.0;    // Bottom Z in micrometers
    double thickness = 1.0;    // Layer thickness in micrometers
    std::string name;
    bool visible = true;

    double z_top() const { return z_bottom + thickness; }

    LayerKey key() const { return LayerKey(layer, datatype); }
};

/**
 * LayerStackup defines the vertical structure of a technology
 */
class LayerStackup {
public:
    LayerStackup() = default;

    // Add a layer to the stackup
    void addLayer(int layer, int datatype, double z_bottom, double thickness,
                  const std::string& name = "");

    // Find elevation for a layer (exact layer/datatype match)
    const LayerElevation* find(int layer, int datatype) const;
    const LayerElevation* find(const LayerKey& key) const;

    // Find elevation by layer number only (any datatype).
    // Returns the first match -- useful as fallback when exact datatype
    // is not mapped (e.g. filler/label datatypes sharing the same z).
    const LayerElevation* findByLayer(int layer) const;

    // Get all layers sorted by z_bottom
    std::vector<LayerElevation> sortedLayers() const;

    // Get total stackup height
    double totalHeight() const;

    // Get number of defined layers
    size_t layerCount() const { return m_layers.size(); }

    // Check if empty
    bool empty() const { return m_layers.empty(); }

    // Clear all layers
    void clear() { m_layers.clear(); }

    // Generate default stackup from .lyp file (basic stacking)
    static LayerStackup fromLayerProperties(const LayerPropertiesFile& lyp,
                                             double default_thickness = 0.5);

    // Load stackup from YAML file
    bool loadFromYAML(const std::string& path);

    // Load from BlenderGDS-format YAML (top-level keys are layer names)
    bool loadFromBlenderGDS(const std::string& path);

private:
    std::map<LayerKey, LayerElevation> m_layers;
};

/**
 * Color entry for a single layer (from BlenderGDS color scheme)
 */
struct LayerColorEntry {
    float color[4] = {0.5f, 0.5f, 0.5f, 1.0f};  // RGBA
    float metallic = 0.0f;
    float roughness = 0.5f;
};

/**
 * Color scheme for layer rendering (loaded from BlenderGDS YAML)
 */
struct LayerColorScheme {
    std::string name;
    std::string description;
    std::map<std::string, LayerColorEntry> layers;  // layer name -> color

    bool loadFromYAML(const std::string& path);
    const LayerColorEntry* find(const std::string& layerName) const;
};

/**
 * Predefined stackups for common technologies
 */
namespace Stackups {

// IHP SG13G2 130nm BiCMOS approximate stackup
LayerStackup createSG13G2();

// Generic interposer stackup
LayerStackup createInterposer();

// Simple 2-metal stackup for testing
LayerStackup createSimple2Metal();

} // namespace Stackups

/**
 * BlenderGDS config file resolution
 */
namespace BlenderGDSConfigs {

// Resolve stackup YAML path for a technology ID
std::string stackupPath(const std::string& techId);

// Resolve color scheme YAML path
std::string colorSchemePath(const std::string& techId,
                            const std::string& scheme = "realistic");

// Resolve the generic black-box color scheme path
// (configs/stackups/colors/generic/<scheme>.yaml). Empty if no configs dir.
std::string genericColorSchemePath(const std::string& scheme = "blackbox");

// Set the configs root directory (default: CONFIGS_DIR compile definition)
void setConfigsDir(const std::string& dir);

} // namespace BlenderGDSConfigs

} // namespace chiplet

#endif // CHIPLET_CORE_LAYERSTACKUP_H
