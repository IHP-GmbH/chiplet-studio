/**
 * test_assembly_2d_view.cpp - Tests for V2 assembly-level 2D view
 *
 * Covers CellComponentMapper, assembly_gds parsing, and DrillDownPanel mode system.
 */

#include <gtest/gtest.h>
#include <QApplication>
#include <QSignalSpy>
#include <QPushButton>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>

#include "view2d/CellComponentMapper.h"
#include "ui/DrillDownPanel.h"
#include "view2d/KLayout2DView.h"
#include "core/Assembly.h"
#include "core/Component.h"
#include "formats/ChipletFormat.h"

using namespace chiplet;

// -- Helper to create an assembly with known component IDs --
static std::unique_ptr<Assembly> makeTestAssembly()
{
    auto assembly = std::make_unique<Assembly>();
    assembly->set_name("Test Assembly");

    assembly->add_component(std::make_unique<Component>("interposer", ComponentType::Interposer));
    assembly->add_component(std::make_unique<Component>("U1", ComponentType::Die));
    assembly->add_component(std::make_unique<Component>("U2", ComponentType::Die));
    assembly->add_component(std::make_unique<Component>("U3", ComponentType::Die));

    return assembly;
}

// ============================================================================
// CellComponentMapper Tests
// ============================================================================

TEST(CellComponentMapper, BuildWithMatchingCells)
{
    auto assembly = makeTestAssembly();
    QStringList cells = {"TOP", "TOP_ROUTING", "CUPILLARS_U1",
                         "U1_Interposer_ANT", "U2_FMD_QNC", "U3_SystemLevel"};

    CellComponentMapper mapper;
    mapper.build(*assembly, cells);

    EXPECT_TRUE(mapper.hasMapping());
    EXPECT_EQ(mapper.componentForCell("U1_Interposer_ANT"), "U1");
    EXPECT_EQ(mapper.componentForCell("U2_FMD_QNC"), "U2");
    EXPECT_EQ(mapper.componentForCell("U3_SystemLevel"), "U3");
}

TEST(CellComponentMapper, UnmappedCellsReturnEmpty)
{
    auto assembly = makeTestAssembly();
    QStringList cells = {"TOP", "TOP_ROUTING", "CUPILLARS_U1",
                         "U1_Interposer_ANT"};

    CellComponentMapper mapper;
    mapper.build(*assembly, cells);

    EXPECT_EQ(mapper.componentForCell("TOP"), "");
    EXPECT_EQ(mapper.componentForCell("TOP_ROUTING"), "");
    EXPECT_EQ(mapper.componentForCell("CUPILLARS_U1"), "");
    EXPECT_EQ(mapper.componentForCell("nonexistent"), "");
}

TEST(CellComponentMapper, EmptyCellList)
{
    auto assembly = makeTestAssembly();
    CellComponentMapper mapper;
    mapper.build(*assembly, {});

    EXPECT_FALSE(mapper.hasMapping());
    EXPECT_EQ(mapper.componentForCell("U1_anything"), "");
}

TEST(CellComponentMapper, ClearResetsMapping)
{
    auto assembly = makeTestAssembly();
    QStringList cells = {"U1_Interposer_ANT"};

    CellComponentMapper mapper;
    mapper.build(*assembly, cells);
    EXPECT_TRUE(mapper.hasMapping());

    mapper.clear();
    EXPECT_FALSE(mapper.hasMapping());
    EXPECT_EQ(mapper.componentForCell("U1_Interposer_ANT"), "");
}

TEST(CellComponentMapper, ComponentToCell)
{
    auto assembly = makeTestAssembly();
    QStringList cells = {"U1_Interposer_ANT", "U2_FMD_QNC", "U3_SystemLevel"};

    CellComponentMapper mapper;
    mapper.build(*assembly, cells);

    EXPECT_EQ(mapper.primaryCellForComponent("U1"), "U1_Interposer_ANT");
    EXPECT_EQ(mapper.primaryCellForComponent("U2"), "U2_FMD_QNC");
    EXPECT_EQ(mapper.primaryCellForComponent("U3"), "U3_SystemLevel");
    EXPECT_EQ(mapper.primaryCellForComponent("nonexistent"), "");
}

TEST(CellComponentMapper, NoMatchingComponents)
{
    auto assembly = makeTestAssembly();
    QStringList cells = {"TOP", "TOP_ROUTING", "SOME_CELL"};

    CellComponentMapper mapper;
    mapper.build(*assembly, cells);

    EXPECT_FALSE(mapper.hasMapping());
}

// ============================================================================
// Assembly GDS in ChipletFormat Tests
// ============================================================================

class AssemblyGdsFormatTest : public ::testing::Test {
protected:
    static int g_argc;
    static char* g_argv[];
    static QApplication* g_app;

    static void SetUpTestSuite() {
        if (!QApplication::instance()) {
            g_app = new QApplication(g_argc, g_argv);
        }
    }
};

int AssemblyGdsFormatTest::g_argc = 0;
char* AssemblyGdsFormatTest::g_argv[] = {nullptr};
QApplication* AssemblyGdsFormatTest::g_app = nullptr;

TEST_F(AssemblyGdsFormatTest, ParseAssemblyGds)
{
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    // Create a dummy GDS file so the path resolves
    QString gdsPath = tmpDir.path() + "/test_complete.gds";
    QFile f(gdsPath);
    f.open(QIODevice::WriteOnly);
    f.close();

    // Write a .chiplet with assembly_gds
    QString chipletPath = tmpDir.path() + "/test.chiplet";
    QFile cf(chipletPath);
    cf.open(QIODevice::WriteOnly | QIODevice::Text);
    cf.write("format_version: \"1.0\"\n"
             "assembly:\n"
             "  name: \"Test\"\n"
             "  assembly_gds: \"test_complete.gds\"\n"
             "  units: \"um\"\n"
             "components:\n"
             "  - id: substrate\n"
             "    type: substrate\n"
             "    dimensions: {width: 100, height: 100, thickness: 50}\n"
             "    position: {x: 0, y: 0, z: 0}\n");
    cf.close();

    ChipletFormat format;
    auto assembly = format.load(chipletPath.toStdString());
    ASSERT_NE(assembly, nullptr);

    // Should resolve relative path to absolute
    EXPECT_FALSE(assembly->assembly_gds().empty());
    EXPECT_TRUE(QString::fromStdString(assembly->assembly_gds()).endsWith("test_complete.gds"));
}

TEST_F(AssemblyGdsFormatTest, ParseAssemblyGdsMissing)
{
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    QString chipletPath = tmpDir.path() + "/test.chiplet";
    QFile cf(chipletPath);
    cf.open(QIODevice::WriteOnly | QIODevice::Text);
    cf.write("format_version: \"1.0\"\n"
             "assembly:\n"
             "  name: \"Test\"\n"
             "  units: \"um\"\n"
             "components:\n"
             "  - id: substrate\n"
             "    type: substrate\n"
             "    dimensions: {width: 100, height: 100, thickness: 50}\n"
             "    position: {x: 0, y: 0, z: 0}\n");
    cf.close();

    ChipletFormat format;
    auto assembly = format.load(chipletPath.toStdString());
    ASSERT_NE(assembly, nullptr);
    EXPECT_TRUE(assembly->assembly_gds().empty());
}

TEST_F(AssemblyGdsFormatTest, SaveAssemblyGds)
{
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    // Create assembly with assembly_gds
    Assembly assembly;
    assembly.set_name("Test");
    assembly.set_assembly_gds("/some/path/complete.gds");

    auto substrate = std::make_unique<Component>("substrate", ComponentType::Substrate);
    substrate->set_dimensions({100, 100, 50});
    assembly.add_component(std::move(substrate));

    // Save
    QString savePath = tmpDir.path() + "/saved.chiplet";
    ChipletFormat format;
    format.save(assembly, savePath.toStdString());

    // Reload and verify
    auto reloaded = format.load(savePath.toStdString());
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(reloaded->assembly_gds(), "/some/path/complete.gds");
}

TEST_F(AssemblyGdsFormatTest, ParseAssemblyGdsRelativePath)
{
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    // Create subdir with GDS
    QDir dir(tmpDir.path());
    dir.mkdir("gds_files");
    QFile f(tmpDir.path() + "/gds_files/assembly.gds");
    f.open(QIODevice::WriteOnly);
    f.close();

    QString chipletPath = tmpDir.path() + "/test.chiplet";
    QFile cf(chipletPath);
    cf.open(QIODevice::WriteOnly | QIODevice::Text);
    cf.write("format_version: \"1.0\"\n"
             "assembly:\n"
             "  name: \"Test\"\n"
             "  assembly_gds: \"gds_files/assembly.gds\"\n"
             "  units: \"um\"\n"
             "components:\n"
             "  - id: substrate\n"
             "    type: substrate\n"
             "    dimensions: {width: 100, height: 100, thickness: 50}\n"
             "    position: {x: 0, y: 0, z: 0}\n");
    cf.close();

    ChipletFormat format;
    auto assembly = format.load(chipletPath.toStdString());
    ASSERT_NE(assembly, nullptr);
    EXPECT_TRUE(QString::fromStdString(assembly->assembly_gds()).contains("gds_files/assembly.gds"));
}

// ============================================================================
// DrillDownPanel Mode Tests
// ============================================================================

class DrillDownPanelModeTest : public ::testing::Test {
protected:
    static int g_argc;
    static char* g_argv[];
    static QApplication* g_app;

    static void SetUpTestSuite() {
        if (!QApplication::instance()) {
            g_app = new QApplication(g_argc, g_argv);
        }
    }
};

int DrillDownPanelModeTest::g_argc = 0;
char* DrillDownPanelModeTest::g_argv[] = {nullptr};
QApplication* DrillDownPanelModeTest::g_app = nullptr;

TEST_F(DrillDownPanelModeTest, DefaultModeIsEmpty)
{
    DrillDownPanel panel;
    EXPECT_EQ(panel.panelMode(), DrillDownPanel::PanelMode::Empty);
}

TEST_F(DrillDownPanelModeTest, SetContextSwitchesToDrillDownMode)
{
    DrillDownPanel panel;
    panel.setContext("U1", "CPU Die", "SG13G2");
    EXPECT_EQ(panel.panelMode(), DrillDownPanel::PanelMode::DrillDown);
}

TEST_F(DrillDownPanelModeTest, ClearContextFromDrillDownReturnsToEmpty)
{
    DrillDownPanel panel;
    // No assembly GDS set, so clearContext should go to Empty
    panel.setContext("U1", "CPU Die", "SG13G2");
    panel.clearContext();
    EXPECT_EQ(panel.panelMode(), DrillDownPanel::PanelMode::Empty);
}

TEST_F(DrillDownPanelModeTest, CellMapperAlwaysValid)
{
    DrillDownPanel panel;
    // cellMapper() should be valid even without assembly GDS
    EXPECT_FALSE(panel.cellMapper().hasMapping());
}

TEST_F(DrillDownPanelModeTest, ComponentNavigatedSignalExists)
{
    DrillDownPanel panel;
    QSignalSpy spy(&panel, &DrillDownPanel::componentNavigated);
    EXPECT_TRUE(spy.isValid());
}
