// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * test_stackup_order_golden.cpp - Freeze the 3D layer stacking order.
 *
 * The vertical order in which a technology's layers stack in the 3D Detailed
 * view is derived ENTIRELY from each layer's z_bottom: LayerStackup::sortedLayers
 * sorts by z_bottom, and LayerMeshBuilder::build re-sorts the built layers by
 * z_bottom. There is no explicit order index. For the bundled PDKs that order
 * comes from the BlenderGDS stackup YAML resolved at "Priority 0"
 * (BlenderGDSConfigs::stackupPath + LayerStackup::loadFromBlenderGDS inside
 * AssemblyView::buildLayerGeometry); the techfile (Priority 1) and hardcoded
 * (Priority 2) fallbacks are never reached while that YAML loads.
 *
 * These tests freeze the resolved order as a golden snapshot so any future
 * change to HOW the stackup is sourced (e.g. collapsing the redundant
 * techfile/hardcoded paths) is forced to keep the exact same layers, z,
 * thickness and names. The snapshot is the contract: it must not change
 * silently.
 *
 * Regenerate the goldens deliberately, then review the git diff:
 *   UPDATE_STACKUP_GOLDEN=1 ./chiplet_tests --gtest_filter='StackupOrderGolden.*'
 */

#include <gtest/gtest.h>
#include "core/LayerStackup.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using namespace chiplet;

namespace {

bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

// Canonical, tie-break-stable serialization of a stackup's layers in stacking
// order. The production sort is by z_bottom alone; layers that share a z_bottom
// are physically co-planar (a drawing layer and its filler), so their relative
// draw order is immaterial. Here we add (layer, datatype) as a deterministic
// tie-break so the snapshot text is stable regardless of std::sort's tie
// behavior. The stacking invariant itself (non-decreasing z_bottom) is asserted
// separately against the unmodified production output.
std::string serializeOrder(const LayerStackup& stackup) {
    std::vector<LayerElevation> layers = stackup.sortedLayers();
    std::sort(layers.begin(), layers.end(),
              [](const LayerElevation& a, const LayerElevation& b) {
                  if (a.z_bottom != b.z_bottom) return a.z_bottom < b.z_bottom;
                  if (a.layer != b.layer) return a.layer < b.layer;
                  return a.datatype < b.datatype;
              });
    std::ostringstream os;
    os << std::fixed << std::setprecision(4);
    for (const auto& l : layers) {
        os << l.z_bottom << '\t' << l.thickness << '\t'
           << l.layer << '/' << l.datatype << '\t' << l.name << '\n';
    }
    return os.str();
}

// Resolve, load, and snapshot one bundled PDK's stackup against its golden.
void checkStackupOrderGolden(const std::string& pdkKey,
                             const std::string& techId,
                             const std::string& expectedBasename) {
    // Read the order/content straight from the bundled YAML so the snapshot is
    // independent of techId substring quirks.
    const std::string contentPath =
        std::string(CONFIGS_DIR) + "/stackups/" + expectedBasename;
    if (!fileExists(contentPath)) {
        GTEST_SKIP() << "Bundled stackup not found: " << contentPath;
    }

    // Also pin the Priority-0 resolution: stackupPath(techId) must point at the
    // same bundled file the app would load for this technology.
    const std::string resolved = BlenderGDSConfigs::stackupPath(techId);
    EXPECT_NE(resolved.find(expectedBasename), std::string::npos)
        << "stackupPath(\"" << techId << "\") resolved to '" << resolved
        << "', expected it to point at " << expectedBasename;

    LayerStackup stackup;
    ASSERT_TRUE(stackup.loadFromBlenderGDS(contentPath))
        << "Failed to load " << contentPath;

    // The stacking invariant: the production order is non-decreasing in z_bottom.
    const std::vector<LayerElevation> prod = stackup.sortedLayers();
    for (size_t i = 1; i < prod.size(); ++i) {
        EXPECT_LE(prod[i - 1].z_bottom, prod[i].z_bottom)
            << "sortedLayers() not monotonic in z_bottom at index " << i
            << " for " << pdkKey;
    }

    const std::string actual = serializeOrder(stackup);
    const std::filesystem::path goldenPath =
        std::filesystem::path(STACKUP_GOLDEN_DIR) / (pdkKey + ".txt");

    if (std::getenv("UPDATE_STACKUP_GOLDEN")) {
        writeFile(goldenPath, actual);
        GTEST_SKIP() << "Captured golden: " << goldenPath.string();
    }

    ASSERT_TRUE(fileExists(goldenPath.string()))
        << "Golden missing: " << goldenPath.string()
        << "\nGenerate it with UPDATE_STACKUP_GOLDEN=1 and review the diff.";

    EXPECT_EQ(actual, readFile(goldenPath.string()))
        << "Stacking order/content changed for " << pdkKey
        << ". If intentional, regenerate with UPDATE_STACKUP_GOLDEN=1 and "
           "review the diff.";
}

}  // namespace

TEST(StackupOrderGolden, IhpSg13g2) {
    checkStackupOrderGolden("ihp-sg13g2", "ihp-sg13g2", "ihp-sg13g2.yaml");
}
TEST(StackupOrderGolden, IhpSg13cmos5l) {
    checkStackupOrderGolden("ihp-sg13cmos5l", "ihp-sg13cmos5l", "ihp-sg13cmos5l.yaml");
}
TEST(StackupOrderGolden, Sky130) {
    checkStackupOrderGolden("sky130", "sky130", "sky130.yaml");
}
TEST(StackupOrderGolden, Gf180mcu) {
    checkStackupOrderGolden("gf180mcu", "gf180mcu", "gf180mcu.yaml");
}
TEST(StackupOrderGolden, Intm4tm2) {
    checkStackupOrderGolden("intm4tm2", "intm4tm2", "intm4tm2.yaml");
}
