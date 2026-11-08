// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * test_fill_and_notes.cpp - Fill in 3D, the per-layer list, and the notes strip.
 *
 * The mesh builder marks fill by its exact key and reports every GDS layer
 * its stackup places nowhere; the Properties layer list marks fill and lists
 * rendered layers the .lyp lacks; the notes strip says what the views could
 * not show. All without a GL context.
 */

#include <gtest/gtest.h>

#include "core/Assembly.h"
#include "core/Component.h"
#include "core/LayerStackup.h"
#include "core/Technology.h"
#include "ui/PropertiesPanel.h"
#include "ui/UnitConverter.h"
#include "ui/ViewingNotesBar.h"
#include "view3d/AssemblyView.h"
#include "view3d/GDSLayerExtractor.h"
#include "view3d/LayerMeshBuilder.h"

#include <QApplication>
#include <QFile>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>

#include <map>

using namespace chiplet;

namespace {

void addSquare(std::map<LayerKey, LayerPolygons>& polys, int layer, int datatype)
{
    SimplePolygon sq;
    sq.points = {Point2D(0.0, 0.0), Point2D(10.0, 0.0), Point2D(10.0, 10.0),
                 Point2D(0.0, 10.0)};
    LayerPolygons& lp = polys[LayerKey(layer, datatype)];
    lp.key = LayerKey(layer, datatype);
    lp.polygons.push_back(sq);
}

const LayerMesh* meshOf(const Component3DGeometry& g, int layer, int datatype)
{
    for (const auto& m : g.layers) {
        if (m.key == LayerKey(layer, datatype)) return &m;
    }
    return nullptr;
}

LayerStackup bundledInterposer()
{
    LayerStackup s;
    s.loadFromBlenderGDS(std::string(CONFIGS_DIR) + "/stackups/intm4tm2.yaml");
    return s;
}

} // namespace

TEST(FillMeshes, FillIsMarkedByItsExactKey)
{
    const LayerStackup stackup = bundledInterposer();
    ASSERT_NE(stackup.find(50, 22), nullptr);

    std::map<LayerKey, LayerPolygons> polys;
    addSquare(polys, 50, 0);
    addSquare(polys, 50, 22);
    addSquare(polys, 134, 22);
    LayerMeshBuilder builder;
    const Component3DGeometry g = builder.build(polys, stackup);

    ASSERT_NE(meshOf(g, 50, 22), nullptr);
    ASSERT_NE(meshOf(g, 134, 22), nullptr);
    ASSERT_NE(meshOf(g, 50, 0), nullptr);
    EXPECT_TRUE(meshOf(g, 50, 22)->fill);
    EXPECT_TRUE(meshOf(g, 134, 22)->fill);
    EXPECT_FALSE(meshOf(g, 50, 0)->fill);
}

// A datatype the stackup does not list borrows its layer number's height, but
// it is not the fill that number's filler entry names.
TEST(FillMeshes, ABorrowedHeightIsNotFill)
{
    LayerStackup stackup;
    stackup.addLayer(77, 22, 1.0, 0.5, "OnlyFill", "filler");
    std::map<LayerKey, LayerPolygons> polys;
    addSquare(polys, 77, 5);
    LayerMeshBuilder builder;
    const Component3DGeometry g = builder.build(polys, stackup);
    const LayerMesh* m = meshOf(g, 77, 5);
    ASSERT_NE(m, nullptr) << "77/5 should draw at 77/22's height";
    EXPECT_FALSE(m->fill);
    EXPECT_TRUE(g.unmodeled.empty());
}

TEST(FillMeshes, LayersTheStackupPlacesNowhereAreReportedNotDropped)
{
    const LayerStackup stackup = bundledInterposer();
    std::map<LayerKey, LayerPolygons> polys;
    addSquare(polys, 50, 0);
    addSquare(polys, 63, 0);
    addSquare(polys, 20, 22);
    polys[LayerKey(235, 4)].key = LayerKey(235, 4);  // listed, but no shapes
    LayerMeshBuilder builder;
    const Component3DGeometry g = builder.build(polys, stackup);

    EXPECT_EQ(g.unmodeled, (std::vector<LayerKey>{LayerKey(20, 22), LayerKey(63, 0)}));
    EXPECT_EQ(meshOf(g, 63, 0), nullptr);
    EXPECT_NE(meshOf(g, 50, 0), nullptr);
}

TEST(FillMeshes, SetFillVisibleTouchesOnlyFill)
{
    const LayerStackup stackup = bundledInterposer();
    std::map<LayerKey, LayerPolygons> polys;
    addSquare(polys, 50, 0);
    addSquare(polys, 50, 22);
    LayerMeshBuilder builder;
    Component3DGeometry g = builder.build(polys, stackup);

    setFillVisible(g, false);
    EXPECT_FALSE(meshOf(g, 50, 22)->visible);
    EXPECT_TRUE(meshOf(g, 50, 0)->visible);
    setFillVisible(g, true);
    EXPECT_TRUE(meshOf(g, 50, 22)->visible);
    EXPECT_TRUE(meshOf(g, 50, 0)->visible);
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

class FillAndNotesWidgets : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("test"), nullptr};
            static QApplication app(argc, argv);
        }
    }
    void SetUp() override { UnitConverter::instance().setUnit(DisplayUnit::Micrometers); }
};

TEST_F(FillAndNotesWidgets, NotesBarIsHiddenWhenThereIsNothingToSay)
{
    ViewingNotesBar bar;
    EXPECT_TRUE(bar.isHidden());

    bar.setNotes({"a", "b", "a", "", "c"});
    EXPECT_FALSE(bar.isHidden());
    EXPECT_EQ(bar.notes(), (QStringList{"a", "b", "c"}));
    EXPECT_EQ(bar.summary(), "3 viewing notes");
    auto* toggle = bar.findChild<QToolButton*>("viewingNotesToggle");
    auto* list = bar.findChild<QPlainTextEdit*>("viewingNotesList");
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(toggle->text(), "3 viewing notes");
    EXPECT_FALSE(toggle->icon().isNull());  // stated by icon and text, not colour
    EXPECT_EQ(list->toPlainText(), "- a\n- b\n- c");

    EXPECT_TRUE(list->isHidden());
    toggle->click();
    EXPECT_TRUE(bar.isExpanded());
    EXPECT_FALSE(list->isHidden());

    bar.setNotes({"only"});
    EXPECT_EQ(bar.summary(), "1 viewing note");

    bar.setNotes({});
    EXPECT_TRUE(bar.isHidden());
    EXPECT_FALSE(bar.isExpanded());
}

TEST_F(FillAndNotesWidgets, PropertiesMarksFillAndListsRenderedLayersTheLypLacks)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const QString lyp = temp.filePath("die.lyp");
    {
        QFile f(lyp);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("<?xml version=\"1.0\"?><layer-properties>"
                "<properties><name>Metal1</name><source>8/0@1</source>"
                "<fill-color>#123456</fill-color><visible>true</visible></properties>"
                "<properties><name>Metal1.filler</name><source>8/22@1</source>"
                "<fill-color>#654321</fill-color><visible>true</visible></properties>"
                "</layer-properties>");
    }

    auto assembly = std::make_unique<Assembly>();
    auto tech = std::make_unique<Technology>("ihp-sg13g2");
    tech->set_layer_properties_path(lyp.toStdString());
    assembly->add_technology(std::move(tech));
    auto die = std::make_unique<Component>("U1", ComponentType::Die);
    die->set_technology("ihp-sg13g2");
    assembly->add_component(std::move(die));

    bool fillShown = false;
    PropertiesPanel panel;
    panel.setLayerVisibilityResolver([&](const QString&, int layer, int datatype) {
        return !(layer == 8 && datatype == 22) || fillShown;
    });
    panel.setLayerListResolver([](const QString&) {
        return std::vector<PropertiesPanel::RenderedLayerInfo>{
            {8, 0, "Metal1", false}, {8, 22, "Metal1Filler", true},
            {500, 0, "CuPillar", false}};
    });
    panel.setComponent("U1", assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>("layerTree");
    ASSERT_NE(tree, nullptr);
    ASSERT_EQ(tree->topLevelItemCount(), 3);
    QTreeWidgetItem* metal = tree->topLevelItem(0);
    QTreeWidgetItem* fill = tree->topLevelItem(1);
    QTreeWidgetItem* pillar = tree->topLevelItem(2);
    EXPECT_EQ(metal->text(0), "Metal1");
    EXPECT_EQ(fill->text(0), "Metal1.filler (fill)");
    EXPECT_EQ(pillar->text(0), "CuPillar");
    EXPECT_EQ(pillar->text(1), "500/0");
    EXPECT_EQ(metal->checkState(0), Qt::Checked);
    EXPECT_EQ(fill->checkState(0), Qt::Unchecked);

    fillShown = true;
    panel.refreshLayerChecks();
    EXPECT_EQ(fill->checkState(0), Qt::Checked);
    EXPECT_EQ(metal->checkState(0), Qt::Checked);
}

TEST_F(FillAndNotesWidgets, PropertiesRowSaysHowMany3DShowsAndHasNoCheckboxWhenItShowsNone)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const QString lyp = temp.filePath("die.lyp");
    {
        QFile f(lyp);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("<?xml version=\"1.0\"?><layer-properties>"
                "<properties><name>Metal1</name><source>8/0@1</source>"
                "<fill-color>#123456</fill-color><visible>true</visible></properties>"
                "</layer-properties>");
    }

    auto assembly = std::make_unique<Assembly>();
    auto tech = std::make_unique<Technology>("ihp-sg13g2");
    tech->set_layer_properties_path(lyp.toStdString());
    assembly->add_technology(std::move(tech));
    auto die = std::make_unique<Component>("U1", ComponentType::Die);
    die->set_technology("ihp-sg13g2");
    assembly->add_component(std::move(die));

    PropertiesPanel panel;
    panel.setLayerVisibilityResolver([](const QString&, int, int) { return true; });
    panel.setLayerListResolver([](const QString&) {
        return std::vector<PropertiesPanel::RenderedLayerInfo>{
            {8, 0, "Metal1", false, "3D shows 10,000 of 412,330 shapes", "metal note", true},
            {19, 0, "Via1", false, "3D shows 0 of 230,112 shapes", "via note", false}};
    });
    panel.setComponent("U1", assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>("layerTree");
    ASSERT_NE(tree, nullptr);
    ASSERT_EQ(tree->topLevelItemCount(), 2);
    QTreeWidgetItem* metal = tree->topLevelItem(0);
    QTreeWidgetItem* via = tree->topLevelItem(1);
    EXPECT_EQ(metal->text(0), "Metal1, 3D shows 10,000 of 412,330 shapes");
    EXPECT_TRUE(metal->toolTip(0).endsWith("\nmetal note"));
    EXPECT_TRUE(metal->data(0, Qt::CheckStateRole).isValid());
    EXPECT_EQ(via->text(0), "Via1, 3D shows 0 of 230,112 shapes");
    EXPECT_EQ(via->text(1), "19/0");
    EXPECT_TRUE(via->toolTip(0).endsWith("\nvia note"));
    EXPECT_FALSE(via->data(0, Qt::CheckStateRole).isValid());
    EXPECT_FALSE(via->flags() & Qt::ItemIsUserCheckable);
    EXPECT_FALSE(via->data(0, Qt::UserRole).isValid());

    // No bulk op gives that row a checkbox or speaks for a layer it lacks.
    std::vector<std::pair<int, int>> changed;
    QObject::connect(&panel, &PropertiesPanel::layerVisibilityChanged,
                     [&](const QString&, int layer, int datatype, bool) {
                         changed.emplace_back(layer, datatype);
                     });
    panel.setAllLayersVisible(false);
    panel.invertLayerVisibility();
    panel.showOnlyMatchingFilter();
    panel.showOnlyLayer(0, 0);
    EXPECT_FALSE(via->data(0, Qt::CheckStateRole).isValid());
    EXPECT_EQ(changed, (std::vector<std::pair<int, int>>{{8, 0}, {8, 0}, {8, 0}}));
}

// The strip names the dies that are not Detailed, so switching fill or a
// die's render mode tells it to refresh. Neither needs the view shown.
TEST_F(FillAndNotesWidgets, FillAndRenderModeSwitchesRefreshTheNotes)
{
    AssemblyView view;
    QSignalSpy spy(&view, &AssemblyView::notesChanged);
    view.setShowFill(true);
    EXPECT_EQ(spy.count(), 1);
    view.onComponentRenderModeChanged(QStringLiteral("U1"), RenderMode::DetailedNoSubstrate);
    EXPECT_EQ(spy.count(), 2);
}
