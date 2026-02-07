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

    // Find elevation for a layer
    const LayerElevation* find(int layer, int datatype) const;
    const LayerElevation* find(const LayerKey& key) const;

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

private:
    std::map<LayerKey, LayerElevation> m_layers;
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

} // namespace chiplet

#endif // CHIPLET_CORE_LAYERSTACKUP_H
