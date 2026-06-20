// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * test_path_contract.cpp - Tripwire for the build/install layout that the
 * adk-tools image hardcodes.
 *
 * The released adk-tools Dockerfile bakes in configs/ and configs/stackups/
 * with a fixed set of stackup fragments, plus the discovery sibling dirnames.
 * Renaming any of them silently breaks the image at build time. These tests
 * fail at ctest (gate 5 of the image build) instead, with a clear message, so
 * a rename is caught here rather than in the downstream image build.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <string>

#include "core/LayerStackup.h"

namespace chiplet {
namespace {

struct StackupCase {
    const char* techId;    // what a .chiplet references
    const char* filename;  // on-disk fragment under configs/stackups/
};

// The five stackup fragments the configs/ layout must keep providing. If any
// techId mapping or filename changes, update adk-tools/Dockerfile in the same
// change (it copies configs/ into the image).
const StackupCase kStackups[] = {
    {"sg13g2",     "ihp-sg13g2.yaml"},
    {"sg13cmos5l", "ihp-sg13cmos5l.yaml"},
    {"sky130",     "sky130.yaml"},
    {"gf180",      "gf180mcu.yaml"},
    {"intm4tm2",   "intm4tm2.yaml"},
};

TEST(PathContract, StackupFragmentsResolveAndLoad)
{
    for (const auto& sc : kStackups) {
        std::string path = BlenderGDSConfigs::stackupPath(sc.techId);
        ASSERT_FALSE(path.empty())
            << "stackupPath('" << sc.techId << "') did not resolve; the "
               "configs/stackups layout or the techId mapping changed.";
        EXPECT_NE(path.find(std::string("stackups/") + sc.filename),
                  std::string::npos)
            << "stackupPath('" << sc.techId << "') = " << path
            << " no longer points at configs/stackups/" << sc.filename;
        ASSERT_TRUE(std::filesystem::exists(path))
            << "Stackup fragment missing on disk: " << path;

        LayerStackup stackup;
        EXPECT_TRUE(stackup.loadFromBlenderGDS(path))
            << "Stackup fragment failed to load: " << path;
        EXPECT_GT(stackup.layerCount(), 0u)
            << "Stackup fragment loaded but has no layers: " << path;
    }
}

} // namespace
} // namespace chiplet
