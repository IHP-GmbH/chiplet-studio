#include <gtest/gtest.h>
#include "process_cfg.h"

class GDS3DLinkageTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(GDS3DLinkageTest, ProcessInstantiation) {
    // Verify GDSProcess can be instantiated
    GDSProcess process;
    // GDSProcess::IsValid() returns true by default (valid but empty)
    // The key test is that LayerCount starts at 0
    EXPECT_EQ(process.LayerCount(), 0) << "Fresh process should have no layers";
}

TEST_F(GDS3DLinkageTest, LoadInterposerTechfile) {
    GDSProcess process;

    // Load the interposer techfile (FIXTURES_DIR is tests/fixtures, pdks is at project root)
    std::string techfile = std::string(FIXTURES_DIR) + "/../../pdks/interposer/techfile/interposer.txt";
    process.Parse(const_cast<char*>(techfile.c_str()));

    EXPECT_TRUE(process.IsValid()) << "Techfile should be valid";
    EXPECT_GT(process.LayerCount(), 0) << "Should have loaded layers";

    // Verify expected layers exist
    ProcessLayer* metal4 = process.GetLayer(50, 0);
    EXPECT_NE(metal4, nullptr) << "Metal4 (layer 50) should exist";

    ProcessLayer* topMetal2 = process.GetLayer(134, 0);
    EXPECT_NE(topMetal2, nullptr) << "TopMetal2 (layer 134) should exist";
}

TEST_F(GDS3DLinkageTest, LoadSG13G2Techfile) {
    GDSProcess process;

    // Load the full SG13G2 techfile (FIXTURES_DIR is tests/fixtures, pdks is at project root)
    std::string techfile = std::string(FIXTURES_DIR) + "/../../pdks/ihp-sg13g2/techfile/sg13g2.txt";
    process.Parse(const_cast<char*>(techfile.c_str()));

    EXPECT_TRUE(process.IsValid()) << "SG13G2 techfile should be valid";
    EXPECT_GT(process.LayerCount(), 10) << "SG13G2 should have many layers";
}
