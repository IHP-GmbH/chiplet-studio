/**
 * LayerStackup.cpp - Implementation
 */

#include "LayerStackup.h"
#include <algorithm>
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace chiplet {

void LayerStackup::addLayer(int layer, int datatype, double z_bottom, double thickness,
                            const std::string& name)
{
    LayerElevation elev;
    elev.layer = layer;
    elev.datatype = datatype;
    elev.z_bottom = z_bottom;
    elev.thickness = thickness;
    elev.name = name;
    elev.visible = true;

    m_layers[elev.key()] = elev;
}

const LayerElevation* LayerStackup::find(int layer, int datatype) const
{
    return find(LayerKey(layer, datatype));
}

const LayerElevation* LayerStackup::find(const LayerKey& key) const
{
    auto it = m_layers.find(key);
    if (it != m_layers.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<LayerElevation> LayerStackup::sortedLayers() const
{
    std::vector<LayerElevation> result;
    result.reserve(m_layers.size());

    for (const auto& [key, elev] : m_layers) {
        result.push_back(elev);
    }

    std::sort(result.begin(), result.end(),
              [](const LayerElevation& a, const LayerElevation& b) {
                  return a.z_bottom < b.z_bottom;
              });

    return result;
}

double LayerStackup::totalHeight() const
{
    double max_z = 0.0;
    for (const auto& [key, elev] : m_layers) {
        double top = elev.z_top();
        if (top > max_z) {
            max_z = top;
        }
    }
    return max_z;
}

LayerStackup LayerStackup::fromLayerProperties(const LayerPropertiesFile& lyp,
                                                double default_thickness)
{
    LayerStackup stackup;

    // Simple approach: stack layers in order based on layer number
    // Higher layer numbers get higher Z positions
    double z = 0.0;

    // Collect and sort layers
    std::vector<const LayerStyle*> sorted_layers;
    for (const auto& style : lyp.layers()) {
        if (style.visible) {
            sorted_layers.push_back(&style);
        }
    }

    std::sort(sorted_layers.begin(), sorted_layers.end(),
              [](const LayerStyle* a, const LayerStyle* b) {
                  return a->key < b->key;
              });

    for (const LayerStyle* style : sorted_layers) {
        stackup.addLayer(style->key.layer, style->key.datatype,
                         z, default_thickness, style->name);
        z += default_thickness;
    }

    return stackup;
}

bool LayerStackup::loadFromYAML(const std::string& path)
{
    try {
        YAML::Node root = YAML::LoadFile(path);

        if (!root["layers"]) {
            return false;
        }

        clear();

        for (const auto& layer_node : root["layers"]) {
            int layer = layer_node["layer"].as<int>(0);
            int datatype = layer_node["datatype"].as<int>(0);
            double z_bottom = layer_node["z_bottom"].as<double>(0.0);
            double thickness = layer_node["thickness"].as<double>(1.0);
            std::string name = layer_node["name"].as<std::string>("");

            addLayer(layer, datatype, z_bottom, thickness, name);
        }

        return true;

    } catch (const std::exception&) {
        return false;
    }
}

// Predefined stackups

namespace Stackups {

LayerStackup createSG13G2()
{
    LayerStackup stackup;

    // IHP SG13G2 approximate layer stackup (simplified)
    // Based on typical BiCMOS process layers

    // Substrate and wells
    stackup.addLayer(1, 0, 0.0, 0.3, "NWell");
    stackup.addLayer(2, 0, 0.0, 0.3, "ThickGateOx");

    // Active and poly
    stackup.addLayer(6, 0, 0.3, 0.1, "Activ");
    stackup.addLayer(5, 0, 0.4, 0.2, "GatPoly");

    // Contacts and vias
    stackup.addLayer(9, 0, 0.6, 0.3, "Cont");

    // Metal layers (M1-M5)
    stackup.addLayer(8, 0, 0.9, 0.4, "Metal1");
    stackup.addLayer(19, 0, 1.3, 0.3, "Via1");
    stackup.addLayer(10, 0, 1.6, 0.5, "Metal2");
    stackup.addLayer(29, 0, 2.1, 0.3, "Via2");
    stackup.addLayer(30, 0, 2.4, 0.5, "Metal3");
    stackup.addLayer(49, 0, 2.9, 0.3, "Via3");
    stackup.addLayer(50, 0, 3.2, 0.5, "Metal4");
    stackup.addLayer(66, 0, 3.7, 0.5, "Via4");
    stackup.addLayer(67, 0, 4.2, 0.8, "Metal5");

    // Top metal
    stackup.addLayer(125, 0, 5.0, 0.6, "TopVia1");
    stackup.addLayer(126, 0, 5.6, 2.0, "TopMetal1");
    stackup.addLayer(133, 0, 7.6, 1.0, "TopVia2");
    stackup.addLayer(134, 0, 8.6, 3.0, "TopMetal2");

    // Passivation
    stackup.addLayer(89, 0, 11.6, 1.0, "Passiv");

    return stackup;
}

LayerStackup createInterposer()
{
    LayerStackup stackup;

    // Generic interposer stackup (RDL layers)
    stackup.addLayer(1, 0, 0.0, 50.0, "Substrate");
    stackup.addLayer(10, 0, 50.0, 2.0, "RDL1");
    stackup.addLayer(20, 0, 52.0, 2.0, "Via1");
    stackup.addLayer(30, 0, 54.0, 2.0, "RDL2");
    stackup.addLayer(40, 0, 56.0, 2.0, "Via2");
    stackup.addLayer(50, 0, 58.0, 3.0, "RDL3");
    stackup.addLayer(60, 0, 61.0, 5.0, "UBM");
    stackup.addLayer(70, 0, 66.0, 30.0, "Bumps");

    return stackup;
}

LayerStackup createSimple2Metal()
{
    LayerStackup stackup;

    stackup.addLayer(1, 0, 0.0, 0.5, "Substrate");
    stackup.addLayer(10, 0, 0.5, 0.3, "Metal1");
    stackup.addLayer(20, 0, 0.8, 0.2, "Via1");
    stackup.addLayer(30, 0, 1.0, 0.4, "Metal2");

    return stackup;
}

} // namespace Stackups

} // namespace chiplet
