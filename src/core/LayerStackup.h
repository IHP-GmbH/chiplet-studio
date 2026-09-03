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

    // Clear all layers and metadata
    void clear() {
        m_layers.clear();
        m_zReference.clear();
        m_attachmentSurfaceZ = 0.0;
        m_hasAttachmentSurfaceZ = false;
    }

    // BlenderGDS scalar metadata (top-level non-map keys):
    //
    // z_reference -- reference frame of this stackup's z values.
    // "attachment_surface" (interconnect fragments) = relative to the
    // surface the base interposer stackup declares; empty = absolute.
    const std::string& zReference() const { return m_zReference; }
    // attachment_surface_z -- the surface (um) an interposer stackup offers
    // to interconnect bodies (the exposed pad top, NOT max_z: passivation
    // around the opening rises above the real mounting surface).
    bool hasAttachmentSurfaceZ() const { return m_hasAttachmentSurfaceZ; }
    double attachmentSurfaceZ() const { return m_attachmentSurfaceZ; }

    // Generate default stackup from .lyp file (basic stacking)
    static LayerStackup fromLayerProperties(const LayerPropertiesFile& lyp,
                                             double default_thickness = 0.5);

    // Load stackup from YAML file
    bool loadFromYAML(const std::string& path);

    // Load from BlenderGDS-format YAML (top-level keys are layer names)
    bool loadFromBlenderGDS(const std::string& path);

    // Load the interconnect PDK's 3D stackup fragment for a fragment key
    // (a method id like "cupillar_opt2", or a legacy adapter id), WITHOUT
    // applying any z offset (z values are as declared in the file; check
    // zReference() for their frame). resolvedPath, when given, receives the
    // fragment path as soon as it resolves (provenance even if the parse
    // fails). Empty stackup when unresolvable.
    static LayerStackup loadInterconnectFragment(const std::string& key,
                                                 std::string* resolvedPath = nullptr);

    // Resolution policy for the active fragment keys -- THE single place
    // that decides which fragments an assembly uses
    // (docs/interconnect_render_contract.md, L2):
    // method ids that resolve to a fragment win; the legacy adapter id is
    // used only when NO method id resolves (it carries the family-default
    // heights, which must not overwrite resolved per-method values).
    // Method ids that do not resolve are dropped with a warning.
    static std::vector<std::string> resolveInterconnectKeys(
        const std::vector<std::string>& methodIds, const std::string& adapter);

    // Load the union of several fragments (raw, no z offset) for UI
    // consumers (layer trees, visibility toggles). Conflicting duplicate
    // keys take the taller body (see mergeInterconnectFragments).
    static LayerStackup loadInterconnectFragments(const std::vector<std::string>& keys);

    // Merge the interconnect fragment for one key into this stackup --
    // THE single entry point for every consumer of the body layers' z
    // (3D render and die-seating z calculation), so their views cannot
    // diverge. Offset rule (docs/interconnect_render_contract.md, L1):
    // fragment z_reference == "attachment_surface" -> offset by this
    // stackup's attachment_surface_z (missing -> warn + best-effort
    // totalHeight()); no marker -> legacy absolute z + deprecation warning.
    // Returns the number of body layers merged (0 = no/empty fragment).
    size_t mergeInterconnectFragment(const std::string& key);

    // Merge the union of several fragments (one per method in use). Body
    // layer keys are disjoint across vendors by manifest construction;
    // options of one family share keys with different heights -- the
    // per-layer render model can only show one height per key, so on a
    // conflicting duplicate the TALLER body wins, loudly (die seating
    // stays exact per die: calculate_component_z merges only the die's
    // own method fragment). Returns the number of layers merged.
    size_t mergeInterconnectFragments(const std::vector<std::string>& keys);

private:
    // The L1 offset rule for one loaded fragment against this base stackup
    // (shared by the single- and multi-fragment merges).
    double interconnectFragmentOffset(const LayerStackup& frag,
                                      const std::string& path) const;

    std::map<LayerKey, LayerElevation> m_layers;
    std::string m_zReference;
    double m_attachmentSurfaceZ = 0.0;
    bool m_hasAttachmentSurfaceZ = false;
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

// Generic interposer stackup (the ladder's last-resort fallback when a
// technology ships neither a YAML stackup nor a .lyp)
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

// Resolve the interconnect PDK 3D stackup fragment for a fragment KEY
// (e.g. "cupillar_opt3" -> interconnect_pdk/libs.tech/chiplet_studio/
// stackup_fragments/cupillar_opt3.stackup.yaml). $INTERCONNECT_PDK_ROOT
// first (set-but-invalid falls through), then a sibling-checkout walk up from
// the configs dir. Empty when unresolvable -- callers skip the merge.
//
// The key is normally a per-die connection METHOD id, not an adapter id: see
// LayerStackup::resolveInterconnectKeys, which tries the method ids first and
// only falls back to the assembly-level adapter when none of them resolves.
// This parameter was called `adapter` until 2026-09; two separate reviewers
// read that name, concluded the studio selects geometry by adapter, and one
// opened a defect on it before anyone read the caller. Geometry resolves by
// METHOD; the adapter axis is DRC and parameters.
std::string interconnectStackupFragmentPath(const std::string& key);

// Resolve the bundled KLayout layer-properties (.lyp) for a supported PDK,
// shipped under pdks/<pdk>/<file>.lyp (sibling of the configs dir). Lets an
// Import GDS of a supported PDK carry the real PDK layer table (names, colors,
// dither) instead of only the stackup-derived layer list. Returns "" when the
// PDK ships no bundled .lyp (e.g. sg13cmos5l, gf180) or the file is missing.
std::string pdkLayerPropertiesPath(const std::string& techId);

// Resolve the generic black-box color scheme path
// (configs/stackups/colors/generic/<scheme>.yaml). Empty if no configs dir.
std::string genericColorSchemePath(const std::string& scheme = "blackbox");

// Set the configs root directory (default: CONFIGS_DIR compile definition)
void setConfigsDir(const std::string& dir);

} // namespace BlenderGDSConfigs

} // namespace chiplet

#endif // CHIPLET_CORE_LAYERSTACKUP_H
