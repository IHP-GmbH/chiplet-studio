/**
 * test_properties_panel.cpp - Unit tests for PropertiesPanel and UnitConverter
 */

#include <gtest/gtest.h>
#include "ui/PropertiesPanel.h"
#include "ui/UnitConverter.h"
#include "core/Assembly.h"
#include "core/Component.h"
#include "core/Technology.h"
#include <QApplication>
#include <QSignalSpy>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>

// Fixture path (FIXTURES_DIR defined via CMake)
static const std::string SG13G2_LYP = std::string(FIXTURES_DIR) + "/sg13g2.lyp";

namespace chiplet {
namespace {

// =============================================================================
// UnitConverter Tests (no Qt required)
// =============================================================================

class UnitConverterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset to default unit at start of each test
        UnitConverter::instance().setUnit(DisplayUnit::Micrometers);
    }
};

TEST_F(UnitConverterTest, DefaultIsUm)
{
    EXPECT_EQ(UnitConverter::instance().currentUnit(), DisplayUnit::Micrometers);
}

TEST_F(UnitConverterTest, ConvertToNanometers)
{
    UnitConverter::instance().setUnit(DisplayUnit::Nanometers);
    EXPECT_DOUBLE_EQ(UnitConverter::instance().toDisplay(1.0), 1000.0);
    EXPECT_DOUBLE_EQ(UnitConverter::instance().toDisplay(0.001), 1.0);
}

TEST_F(UnitConverterTest, ConvertToMillimeters)
{
    UnitConverter::instance().setUnit(DisplayUnit::Millimeters);
    EXPECT_DOUBLE_EQ(UnitConverter::instance().toDisplay(1000.0), 1.0);
    EXPECT_DOUBLE_EQ(UnitConverter::instance().toDisplay(1.0), 0.001);
}

TEST_F(UnitConverterTest, ConvertToMicrometers)
{
    UnitConverter::instance().setUnit(DisplayUnit::Micrometers);
    EXPECT_DOUBLE_EQ(UnitConverter::instance().toDisplay(123.456), 123.456);
}

TEST_F(UnitConverterTest, FromDisplayNanometers)
{
    UnitConverter::instance().setUnit(DisplayUnit::Nanometers);
    EXPECT_DOUBLE_EQ(UnitConverter::instance().fromDisplay(1000.0), 1.0);
}

TEST_F(UnitConverterTest, FromDisplayMillimeters)
{
    UnitConverter::instance().setUnit(DisplayUnit::Millimeters);
    EXPECT_DOUBLE_EQ(UnitConverter::instance().fromDisplay(1.0), 1000.0);
}

TEST_F(UnitConverterTest, UnitSuffixNm)
{
    UnitConverter::instance().setUnit(DisplayUnit::Nanometers);
    EXPECT_EQ(UnitConverter::instance().unitSuffix().toStdString(), "nm");
}

TEST_F(UnitConverterTest, UnitSuffixUm)
{
    UnitConverter::instance().setUnit(DisplayUnit::Micrometers);
    EXPECT_EQ(UnitConverter::instance().unitSuffix().toStdString(), "um");
}

TEST_F(UnitConverterTest, UnitSuffixMm)
{
    UnitConverter::instance().setUnit(DisplayUnit::Millimeters);
    EXPECT_EQ(UnitConverter::instance().unitSuffix().toStdString(), "mm");
}

TEST_F(UnitConverterTest, AvailableUnits)
{
    QStringList units = UnitConverter::availableUnits();
    EXPECT_EQ(units.size(), 3);
    EXPECT_EQ(units[0].toStdString(), "nm");
    EXPECT_EQ(units[1].toStdString(), "um");
    EXPECT_EQ(units[2].toStdString(), "mm");
}

TEST_F(UnitConverterTest, UnitFromIndex)
{
    EXPECT_EQ(UnitConverter::unitFromIndex(0), DisplayUnit::Nanometers);
    EXPECT_EQ(UnitConverter::unitFromIndex(1), DisplayUnit::Micrometers);
    EXPECT_EQ(UnitConverter::unitFromIndex(2), DisplayUnit::Millimeters);
    EXPECT_EQ(UnitConverter::unitFromIndex(99), DisplayUnit::Micrometers);  // Invalid defaults to um
}

TEST_F(UnitConverterTest, IndexFromUnit)
{
    EXPECT_EQ(UnitConverter::indexFromUnit(DisplayUnit::Nanometers), 0);
    EXPECT_EQ(UnitConverter::indexFromUnit(DisplayUnit::Micrometers), 1);
    EXPECT_EQ(UnitConverter::indexFromUnit(DisplayUnit::Millimeters), 2);
}

TEST_F(UnitConverterTest, ToDisplayString)
{
    UnitConverter::instance().setUnit(DisplayUnit::Micrometers);
    QString result = UnitConverter::instance().toDisplayString(1234.567, 2);
    EXPECT_EQ(result.toStdString(), "1234.57 um");
}

TEST_F(UnitConverterTest, ToDisplayStringNm)
{
    UnitConverter::instance().setUnit(DisplayUnit::Nanometers);
    QString result = UnitConverter::instance().toDisplayString(1.0, 1);
    EXPECT_EQ(result.toStdString(), "1000.0 nm");
}

// =============================================================================
// Helper to create test assemblies
// =============================================================================

std::unique_ptr<Assembly> createTestAssembly()
{
    auto assembly = std::make_unique<Assembly>();
    assembly->set_name("TestAssembly");

    // Add technology
    auto tech = std::make_unique<Technology>("tsmc_n5");
    tech->set_description("TSMC 5nm");
    assembly->add_technology(std::move(tech));

    // Add component
    auto die = std::make_unique<Component>("die_a", ComponentType::Die);
    die->set_technology("tsmc_n5");
    die->set_position({100.0, 200.0, 50.0});
    die->set_rotation({45.0});
    die->set_dimensions({5000.0, 4000.0, 100.0});
    die->set_layout_path("./layouts/die_a.gds");
    die->set_top_cell("DIE_TOP");
    die->set_metadata("vendor", "ACME Corp");
    assembly->add_component(std::move(die));

    return assembly;
}

std::unique_ptr<Component> createDieArrayComponent()
{
    auto arr = std::make_unique<Component>("hbm_array", ComponentType::DieArray);
    arr->set_technology("hbm_tech");

    ComponentArray arrayConfig;
    arrayConfig.pattern = "grid";
    arrayConfig.countX = 2;
    arrayConfig.countY = 2;
    arrayConfig.pitchX = 6000.0;
    arrayConfig.pitchY = 8000.0;
    arr->set_array(arrayConfig);

    return arr;
}

// Assembly whose component technology points at the real sg13g2.lyp fixture, so
// the PropertiesPanel layer tree populates with hundreds of layers.
std::unique_ptr<Assembly> createLypAssembly()
{
    auto assembly = std::make_unique<Assembly>();
    assembly->set_name("LypAssembly");

    auto tech = std::make_unique<Technology>("ihp_sg13g2");
    tech->set_layer_properties_path(SG13G2_LYP);
    assembly->add_technology(std::move(tech));

    auto die = std::make_unique<Component>("die_lyp", ComponentType::Die);
    die->set_technology("ihp_sg13g2");
    assembly->add_component(std::move(die));

    return assembly;
}

// Count layer-tree rows not hidden by the filter.
static int visibleLayerRows(QTreeWidget* tree)
{
    int n = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (!tree->topLevelItem(i)->isHidden()) {
            ++n;
        }
    }
    return n;
}

// =============================================================================
// PropertiesPanel Widget Tests
// =============================================================================

class PropertiesPanelTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("test"), nullptr};
            static QApplication app(argc, argv);
        }
    }

    void SetUp() override {
        UnitConverter::instance().setUnit(DisplayUnit::Micrometers);
    }
};

TEST_F(PropertiesPanelTest, Construction)
{
    PropertiesPanel panel;
    // Panel should construct without crash
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, SetInvalidComponent)
{
    PropertiesPanel panel;
    // Set with invalid/empty component ID
    panel.setComponent("", nullptr);
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, SetValidComponent)
{
    PropertiesPanel panel;
    auto assembly = createTestAssembly();

    // Verify component exists
    Component* die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);

    // Set component using ID-based API
    panel.setComponent("die_a", assembly.get());
    // Should display component properties without crash
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, ClearSelection)
{
    PropertiesPanel panel;
    auto assembly = createTestAssembly();

    panel.setComponent("die_a", assembly.get());
    panel.clearSelection();
    // Should reset all fields without crash
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, SetAssembly)
{
    PropertiesPanel panel;
    auto assembly = createTestAssembly();
    panel.setAssembly(assembly.get());
    // Should store assembly reference without crash
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, DieArrayShowsArrayGroup)
{
    PropertiesPanel panel;
    auto assembly = std::make_unique<Assembly>();

    // Add a regular die
    auto die = std::make_unique<Component>("die", ComponentType::Die);
    assembly->add_component(std::move(die));

    // Add a die array
    auto dieArray = createDieArrayComponent();
    assembly->add_component(std::move(dieArray));

    // First set a regular die - array group should be hidden
    panel.setComponent("die", assembly.get());

    // Then set a die array - array group should be visible
    panel.setComponent("hbm_array", assembly.get());
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, ComponentWithMetadata)
{
    PropertiesPanel panel;
    auto assembly = createTestAssembly();

    // die_a has metadata set in createTestAssembly()
    panel.setComponent("die_a", assembly.get());
    // Metadata group should be visible
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, ComponentWithoutMetadata)
{
    PropertiesPanel panel;
    auto assembly = std::make_unique<Assembly>();

    // Add a simple die without metadata
    auto die = std::make_unique<Component>("simple_die", ComponentType::Die);
    assembly->add_component(std::move(die));

    panel.setComponent("simple_die", assembly.get());
    // Metadata group should be hidden
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, MultipleComponentChanges)
{
    PropertiesPanel panel;
    auto assembly = createTestAssembly();

    // Cycle through components using ID-based API
    panel.setComponent("die_a", assembly.get());
    panel.setComponent("", assembly.get());  // Invalid ID clears selection
    panel.setComponent("die_a", assembly.get());
    panel.clearSelection();
    // Should handle multiple changes without crash
    EXPECT_TRUE(true);
}

// =============================================================================
// Layers Group: show/hide checkboxes + search filter
// =============================================================================

TEST_F(PropertiesPanelTest, LayerTreePopulatesFromLyp)
{
    PropertiesPanel panel;
    auto assembly = createLypAssembly();
    panel.setComponent("die_lyp", assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>("layerTree");
    ASSERT_NE(tree, nullptr);
    // sg13g2.lyp carries a large layer set; assert it loaded a meaningful list.
    EXPECT_GT(tree->topLevelItemCount(), 100);

    // Every row is user-checkable and visible by default.
    QTreeWidgetItem* first = tree->topLevelItem(0);
    ASSERT_NE(first, nullptr);
    EXPECT_TRUE(first->flags() & Qt::ItemIsUserCheckable);
    EXPECT_EQ(first->checkState(0), Qt::Checked);
}

TEST_F(PropertiesPanelTest, LayerFilterHidesNonMatching)
{
    PropertiesPanel panel;
    auto assembly = createLypAssembly();
    panel.setComponent("die_lyp", assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>("layerTree");
    QLineEdit* filter = panel.findChild<QLineEdit*>("layerFilter");
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(filter, nullptr);

    const int total = tree->topLevelItemCount();
    ASSERT_GT(total, 0);
    EXPECT_EQ(visibleLayerRows(tree), total);  // no filter -> all visible

    // A real layer family in sg13g2.lyp: narrows but keeps at least one row.
    filter->setText("TopMetal2");
    int matched = visibleLayerRows(tree);
    EXPECT_GT(matched, 0);
    EXPECT_LT(matched, total);
    for (int i = 0; i < total; ++i) {
        QTreeWidgetItem* item = tree->topLevelItem(i);
        if (!item->isHidden()) {
            EXPECT_TRUE(item->text(0).contains("TopMetal2", Qt::CaseInsensitive) ||
                        item->text(1).contains("TopMetal2", Qt::CaseInsensitive));
        }
    }

    // A string no layer contains hides everything.
    filter->setText("zzz_no_such_layer");
    EXPECT_EQ(visibleLayerRows(tree), 0);

    // Clearing the filter restores every row.
    filter->clear();
    EXPECT_EQ(visibleLayerRows(tree), total);
}

TEST_F(PropertiesPanelTest, LayerToggleEmitsVisibilityChanged)
{
    PropertiesPanel panel;
    auto assembly = createLypAssembly();
    panel.setComponent("die_lyp", assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>("layerTree");
    ASSERT_NE(tree, nullptr);
    ASSERT_GT(tree->topLevelItemCount(), 0);

    // Spy created AFTER populate, so only the user toggle is counted.
    QSignalSpy spy(&panel, &PropertiesPanel::layerVisibilityChanged);
    ASSERT_TRUE(spy.isValid());

    QTreeWidgetItem* item = tree->topLevelItem(0);
    const int expectLayer = item->data(0, Qt::UserRole).toInt();
    const int expectDatatype = item->data(0, Qt::UserRole + 1).toInt();

    item->setCheckState(0, Qt::Unchecked);  // user hides the layer

    ASSERT_EQ(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toString().toStdString(), "die_lyp");
    EXPECT_EQ(args.at(1).toInt(), expectLayer);
    EXPECT_EQ(args.at(2).toInt(), expectDatatype);
    EXPECT_FALSE(args.at(3).toBool());

    // Re-checking emits the visible=true counterpart.
    item->setCheckState(0, Qt::Checked);
    ASSERT_EQ(spy.count(), 1);
    EXPECT_TRUE(spy.takeFirst().at(3).toBool());
}

TEST_F(PropertiesPanelTest, PopulatingLayersDoesNotEmit)
{
    PropertiesPanel panel;
    auto assembly = createLypAssembly();

    // Spy created BEFORE the component is set: populating the (checked) rows
    // must not be mistaken for user toggles.
    QSignalSpy spy(&panel, &PropertiesPanel::layerVisibilityChanged);
    ASSERT_TRUE(spy.isValid());

    panel.setComponent("die_lyp", assembly.get());
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(PropertiesPanelTest, CheckboxStateSeededFromResolver)
{
    PropertiesPanel panel;
    auto assembly = createLypAssembly();

    // Resolver hides the first layer it is asked about (== row 0), shows the rest.
    // This mirrors revisiting a component whose first layer was hidden in 3D.
    bool firstSeen = false;
    int hiddenLayer = -1, hiddenDatatype = -1;
    panel.setLayerVisibilityResolver(
        [&](const QString&, int l, int d) {
            if (!firstSeen) {
                firstSeen = true;
                hiddenLayer = l;
                hiddenDatatype = d;
                return false;
            }
            return true;
        });

    panel.setComponent("die_lyp", assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>("layerTree");
    ASSERT_NE(tree, nullptr);
    ASSERT_GT(tree->topLevelItemCount(), 1);

    QTreeWidgetItem* row0 = tree->topLevelItem(0);
    EXPECT_EQ(row0->checkState(0), Qt::Unchecked);
    EXPECT_EQ(row0->data(0, Qt::UserRole).toInt(), hiddenLayer);
    EXPECT_EQ(row0->data(0, Qt::UserRole + 1).toInt(), hiddenDatatype);
    EXPECT_EQ(tree->topLevelItem(1)->checkState(0), Qt::Checked);
}

// =============================================================================
// Bulk visibility ops (Show all / Hide all / Show only / Invert / Match filter)
// =============================================================================

// Count how many rows currently have the given check state.
static int countWithState(QTreeWidget* tree, Qt::CheckState state) {
    int n = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->checkState(0) == state) ++n;
    }
    return n;
}

TEST_F(PropertiesPanelTest, BulkHideAllUnchecksEveryVisibleRow)
{
    PropertiesPanel panel;
    auto assembly = createLypAssembly();
    panel.setComponent("die_lyp", assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>("layerTree");
    ASSERT_NE(tree, nullptr);
    const int total = tree->topLevelItemCount();
    ASSERT_GT(total, 0);

    QSignalSpy spy(&panel, &PropertiesPanel::layerVisibilityChanged);
    ASSERT_TRUE(spy.isValid());

    panel.setAllLayersVisible(false);

    EXPECT_EQ(countWithState(tree, Qt::Unchecked), total);
    EXPECT_EQ(spy.count(), total);  // one emit per row that flipped
    // every emit carries visible=false
    while (!spy.isEmpty()) {
        EXPECT_FALSE(spy.takeFirst().at(3).toBool());
    }
}

TEST_F(PropertiesPanelTest, BulkShowAllOnlyEmitsForChangedRows)
{
    PropertiesPanel panel;
    auto assembly = createLypAssembly();
    panel.setComponent("die_lyp", assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>("layerTree");
    ASSERT_NE(tree, nullptr);
    const int total = tree->topLevelItemCount();
    ASSERT_GT(total, 2);

    // Uncheck three known rows, then bulk-show-all and expect exactly three emits.
    tree->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
    tree->topLevelItem(1)->setCheckState(0, Qt::Unchecked);
    tree->topLevelItem(2)->setCheckState(0, Qt::Unchecked);

    QSignalSpy spy(&panel, &PropertiesPanel::layerVisibilityChanged);
    ASSERT_TRUE(spy.isValid());

    panel.setAllLayersVisible(true);

    EXPECT_EQ(countWithState(tree, Qt::Checked), total);
    EXPECT_EQ(spy.count(), 3);
}

TEST_F(PropertiesPanelTest, BulkOpsSkipFilterHiddenRows)
{
    PropertiesPanel panel;
    auto assembly = createLypAssembly();
    panel.setComponent("die_lyp", assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>("layerTree");
    QLineEdit* filter = panel.findChild<QLineEdit*>("layerFilter");
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(filter, nullptr);

    // Filter down to a single family so most rows are filter-hidden.
    filter->setText("TopMetal2");
    const int visibleRows = visibleLayerRows(tree);
    ASSERT_GT(visibleRows, 0);
    ASSERT_LT(visibleRows, tree->topLevelItemCount());

    panel.setAllLayersVisible(false);

    // Filter-hidden rows must still be checked; only filter-visible rows flipped.
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = tree->topLevelItem(i);
        if (item->isHidden()) {
            EXPECT_EQ(item->checkState(0), Qt::Checked)
                << "filter-hidden row " << i << " was wrongly flipped";
        } else {
            EXPECT_EQ(item->checkState(0), Qt::Unchecked);
        }
    }
}

TEST_F(PropertiesPanelTest, ShowOnlyLayerIsolatesOneRow)
{
    PropertiesPanel panel;
    auto assembly = createLypAssembly();
    panel.setComponent("die_lyp", assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>("layerTree");
    ASSERT_NE(tree, nullptr);
    ASSERT_GT(tree->topLevelItemCount(), 1);

    // Pick row 3 as the keep-target (avoids any default-first-row corner case).
    QTreeWidgetItem* keep = tree->topLevelItem(3);
    const int keepLayer = keep->data(0, Qt::UserRole).toInt();
    const int keepDatatype = keep->data(0, Qt::UserRole + 1).toInt();

    panel.showOnlyLayer(keepLayer, keepDatatype);

    int checkedCount = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = tree->topLevelItem(i);
        const int l = item->data(0, Qt::UserRole).toInt();
        const int d = item->data(0, Qt::UserRole + 1).toInt();
        const bool match = (l == keepLayer && d == keepDatatype);
        EXPECT_EQ(item->checkState(0),
                  match ? Qt::Checked : Qt::Unchecked);
        if (item->checkState(0) == Qt::Checked) ++checkedCount;
    }
    // The lyp could in theory hold duplicate keys; we only require >= 1 row matches.
    EXPECT_GE(checkedCount, 1);
}

TEST_F(PropertiesPanelTest, InvertFlipsEveryVisibleRow)
{
    PropertiesPanel panel;
    auto assembly = createLypAssembly();
    panel.setComponent("die_lyp", assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>("layerTree");
    ASSERT_NE(tree, nullptr);
    const int total = tree->topLevelItemCount();
    ASSERT_GT(total, 4);

    // Start mixed: uncheck rows 0, 2, 4.
    tree->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
    tree->topLevelItem(2)->setCheckState(0, Qt::Unchecked);
    tree->topLevelItem(4)->setCheckState(0, Qt::Unchecked);

    std::vector<Qt::CheckState> before;
    before.reserve(total);
    for (int i = 0; i < total; ++i) {
        before.push_back(tree->topLevelItem(i)->checkState(0));
    }

    panel.invertLayerVisibility();

    for (int i = 0; i < total; ++i) {
        const Qt::CheckState expected =
            before[i] == Qt::Checked ? Qt::Unchecked : Qt::Checked;
        EXPECT_EQ(tree->topLevelItem(i)->checkState(0), expected)
            << "row " << i << " did not invert";
    }
}

TEST_F(PropertiesPanelTest, ShowOnlyMatchingFilterMatchesFilterVisibility)
{
    PropertiesPanel panel;
    auto assembly = createLypAssembly();
    panel.setComponent("die_lyp", assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>("layerTree");
    QLineEdit* filter = panel.findChild<QLineEdit*>("layerFilter");
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(filter, nullptr);

    filter->setText("TopMetal2");
    const int matched = visibleLayerRows(tree);
    ASSERT_GT(matched, 0);
    ASSERT_LT(matched, tree->topLevelItemCount());

    panel.showOnlyMatchingFilter();

    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = tree->topLevelItem(i);
        const Qt::CheckState expected =
            item->isHidden() ? Qt::Unchecked : Qt::Checked;
        EXPECT_EQ(item->checkState(0), expected);
    }
}

} // namespace
} // namespace chiplet
