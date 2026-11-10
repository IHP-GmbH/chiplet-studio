// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * test_assembly_sheet.cpp - One 2D layer list for a layout that mixes
 * technologies.
 *
 * composeAssemblySheet decides, per GDS layer, its group, whose sheet styles
 * it and whether it starts visible; measureOccupancy says which side of the
 * assembly has shapes on it. Both run without a view. The last test loads a
 * real layout into KLayout2DView under the gate's display.
 */

#include <gtest/gtest.h>

#include "view2d/AssemblySheet.h"
#include "view2d/KLayout2DView.h"
#include "core/Assembly.h"
#include "core/Component.h"

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>

#include <stdexcept>

#ifdef HAVE_KLAYOUT
#include "dbBox.h"
#include "dbCell.h"
#include "dbCellInst.h"
#include "dbLayerProperties.h"
#include "dbLayout.h"
#include "dbWriter.h"
#include "tlStream.h"
#endif

using namespace chiplet;

namespace {

const char kCarrier[] = "intm4tm2";
const char kDie[] = "ihp-sg13g2";

SheetTech sheet(std::initializer_list<std::pair<LayerNum, std::string>> entries,
                std::set<LayerNum> fill = {})
{
    SheetTech s;
    for (const auto& [key, name] : entries) {
        s.order.push_back(key);
        s.names[key] = name;
        s.visible[key] = true;
    }
    s.fill = std::move(fill);
    return s;
}

LayerUse use(int layer, int datatype, bool carrier, std::set<std::string> dies = {})
{
    LayerUse u;
    u.key = LayerNum(layer, datatype);
    u.carrier = carrier;
    u.dieTechs = std::move(dies);
    return u;
}

const ComposedRow* rowOf(const std::vector<ComposedGroup>& groups, int layer, int datatype,
                         std::string* group = nullptr)
{
    for (const auto& g : groups) {
        for (const auto& r : g.rows) {
            if (r.key == LayerNum(layer, datatype)) {
                if (group) *group = g.title;
                return &r;
            }
        }
    }
    return nullptr;
}

std::map<std::string, SheetTech> twoSheets()
{
    std::map<std::string, SheetTech> sheets;
    sheets[kCarrier] = sheet({{{126, 0}, "TopMetal1"}, {{134, 0}, "TopMetal2"},
                              {{50, 22}, "Metal4.filler"}},
                             {{50, 22}});
    sheets[kDie] = sheet({{{8, 0}, "Metal1"}, {{126, 0}, "TopMetal1.drawing"},
                          {{8, 22}, "Metal1.filler"}},
                         {{8, 22}});
    return sheets;
}

} // namespace

TEST(AssemblySheet, CarrierOnlyLayerIsGroupedAndStyledByTheCarrier)
{
    const auto groups = composeAssemblySheet({use(134, 0, true)}, {kCarrier}, twoSheets());
    std::string group;
    const ComposedRow* r = rowOf(groups, 134, 0, &group);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(group, kCarrier);
    EXPECT_EQ(r->styleTech, kCarrier);
    EXPECT_EQ(r->name, "TopMetal2");
    EXPECT_EQ(r->owners, std::vector<std::string>{kCarrier});
    EXPECT_TRUE(r->visible);
    EXPECT_FALSE(r->fill);
}

TEST(AssemblySheet, DieOnlyLayerIsStyledByTheDieSheet)
{
    const auto groups = composeAssemblySheet({use(8, 0, false, {kDie})}, {kCarrier},
                                             twoSheets());
    std::string group;
    const ComposedRow* r = rowOf(groups, 8, 0, &group);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(group, kDie);
    EXPECT_EQ(r->styleTech, kDie);
    EXPECT_EQ(r->name, "Metal1");
}

TEST(AssemblySheet, LayerOnBothSidesIsOneSharedRowStyledByTheCarrierNamingBoth)
{
    const auto groups = composeAssemblySheet({use(126, 0, true, {kDie})}, {kCarrier},
                                             twoSheets());
    std::string group;
    const ComposedRow* r = rowOf(groups, 126, 0, &group);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(group, kSharedGroup);
    EXPECT_EQ(r->styleTech, kCarrier);
    EXPECT_EQ(r->name, "TopMetal1 (shared: intm4tm2, ihp-sg13g2)");
    EXPECT_EQ(r->owners, (std::vector<std::string>{kCarrier, kDie}));

    int rows = 0;
    for (const auto& g : groups) rows += static_cast<int>(g.rows.size());
    EXPECT_EQ(rows, 1);
}

TEST(AssemblySheet, LayerNoSheetNamesKeepsDefaultStyling)
{
    const auto groups = composeAssemblySheet({use(235, 4, true)}, {kCarrier}, twoSheets());
    std::string group;
    const ComposedRow* r = rowOf(groups, 235, 4, &group);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(group, kCarrier);
    EXPECT_EQ(r->styleTech, "");
    EXPECT_EQ(r->name, "(default styling)");
    EXPECT_TRUE(r->visible);
}

TEST(AssemblySheet, FillStartsHiddenOnEitherSide)
{
    const auto groups = composeAssemblySheet(
        {use(50, 22, true), use(8, 22, false, {kDie}), use(134, 0, true)}, {kCarrier},
        twoSheets());
    const ComposedRow* carrierFill = rowOf(groups, 50, 22);
    const ComposedRow* dieFill = rowOf(groups, 8, 22);
    const ComposedRow* metal = rowOf(groups, 134, 0);
    ASSERT_NE(carrierFill, nullptr);
    ASSERT_NE(dieFill, nullptr);
    ASSERT_NE(metal, nullptr);
    EXPECT_TRUE(carrierFill->fill);
    EXPECT_FALSE(carrierFill->visible);
    EXPECT_EQ(carrierFill->name, "Metal4.filler (fill)");
    EXPECT_TRUE(dieFill->fill);
    EXPECT_FALSE(dieFill->visible);
    EXPECT_FALSE(metal->fill);
    EXPECT_TRUE(metal->visible);
}

// The stackup decides fill even where the sheet has no entry for the layer.
TEST(AssemblySheet, FillWithoutASheetEntryIsStillFill)
{
    auto sheets = twoSheets();
    sheets[kCarrier].fill.insert({67, 22});
    const auto groups = composeAssemblySheet({use(67, 22, true)}, {kCarrier}, sheets);
    const ComposedRow* r = rowOf(groups, 67, 22);
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(r->fill);
    EXPECT_FALSE(r->visible);
    EXPECT_EQ(r->name, "(default styling) (fill)");
}

TEST(AssemblySheet, GroupsComeCarrierSharedDiesUnassigned)
{
    // No carrier technology known: carrier shapes are Unassigned.
    const auto noCarrier = composeAssemblySheet({use(134, 0, true)}, {}, twoSheets());
    ASSERT_EQ(noCarrier.size(), 1u);
    EXPECT_EQ(noCarrier[0].title, kUnassignedGroup);

    const auto groups = composeAssemblySheet(
        {use(8, 0, false, {kDie}), use(126, 0, true, {kDie}), use(134, 0, true),
         use(1, 0, false, {"acme-node"})},
        {kCarrier}, twoSheets());
    std::vector<std::string> titles;
    for (const auto& g : groups) titles.push_back(g.title);
    EXPECT_EQ(titles, (std::vector<std::string>{kCarrier, kSharedGroup, "acme-node", kDie}));
}

#ifdef HAVE_KLAYOUT

namespace {

// Top cell with carrier shapes; die U1 as a cell named U1_..., with one layer
// only in a child cell; die U2 has no cell at all.
void buildAssembly(db::Layout& layout, db::cell_index_type& top)
{
    layout.dbu(0.001);
    top = layout.add_cell("TOP");
    const auto u1 = layout.add_cell("U1_layout_flipped");
    const auto u1child = layout.add_cell("U1_inner");
    const auto pillar = layout.add_cell("PILLAR");
    const auto tm1 = layout.insert_layer(db::LayerProperties(126, 0));
    const auto m4fill = layout.insert_layer(db::LayerProperties(50, 22));
    const auto m1 = layout.insert_layer(db::LayerProperties(8, 0));
    const auto m2 = layout.insert_layer(db::LayerProperties(10, 0));
    const auto bump = layout.insert_layer(db::LayerProperties(500, 0));
    layout.cell(top).shapes(tm1).insert(db::Box(0, 0, 100000, 1000));
    layout.cell(top).shapes(m4fill).insert(db::Box(0, 2000, 2000, 4000));
    layout.cell(pillar).shapes(bump).insert(db::Box(0, 0, 5000, 5000));
    layout.cell(u1).shapes(tm1).insert(db::Box(0, 0, 10000, 10000));
    layout.cell(u1).shapes(m1).insert(db::Box(0, 0, 10000, 10000));
    layout.cell(u1child).shapes(m2).insert(db::Box(0, 0, 1000, 1000));
    layout.cell(u1).insert(db::CellInstArray(db::CellInst(u1child), db::Trans()));
    layout.cell(top).insert(db::CellInstArray(db::CellInst(u1), db::Trans()));
    layout.cell(top).insert(db::CellInstArray(db::CellInst(pillar), db::Trans()));
}

const LayerUse* useOf(const Occupancy& occ, int layer, int datatype)
{
    for (const auto& u : occ.uses) {
        if (u.key == LayerNum(layer, datatype)) return &u;
    }
    return nullptr;
}

} // namespace

TEST(AssemblySheet, OccupancySplitsCarrierAndDieByCellPrefix)
{
    db::Layout layout;
    db::cell_index_type top = 0;
    buildAssembly(layout, top);
    const Occupancy occ = measureOccupancy(layout, top, {{"U1", kDie}, {"U2", kDie}});

    const LayerUse* tm1 = useOf(occ, 126, 0);
    const LayerUse* fill = useOf(occ, 50, 22);
    const LayerUse* m1 = useOf(occ, 8, 0);
    const LayerUse* m2 = useOf(occ, 10, 0);
    const LayerUse* bump = useOf(occ, 500, 0);
    ASSERT_TRUE(tm1 && fill && m1 && m2 && bump);

    EXPECT_TRUE(tm1->carrier);                       // top cell
    EXPECT_EQ(tm1->dieTechs, std::set<std::string>{kDie});  // and U1
    EXPECT_TRUE(fill->carrier);
    EXPECT_TRUE(fill->dieTechs.empty());
    EXPECT_FALSE(m1->carrier);
    EXPECT_EQ(m1->dieTechs, std::set<std::string>{kDie});
    // Only under the die cell's child: counted for the die, not the carrier.
    EXPECT_FALSE(m2->carrier);
    EXPECT_EQ(m2->dieTechs, std::set<std::string>{kDie});
    // A non-die child of the top cell is the carrier.
    EXPECT_TRUE(bump->carrier);

    EXPECT_EQ(occ.unmatchedDies, std::vector<std::string>{"U2"});
}

namespace {

void writeAssembly(const QString& path)
{
    db::Layout layout;
    db::cell_index_type top = 0;
    buildAssembly(layout, top);
    tl::OutputStream stream(path.toStdString());
    db::SaveLayoutOptions options;
    db::Writer writer(options);
    writer.write(layout, stream);
}

void writeSheet(const QString& path, const char* body)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        throw std::runtime_error("sheet write failed");
    file.write("<?xml version=\"1.0\"?><layer-properties>");
    file.write(body);
    file.write("</layer-properties>");
}

const LayerInfo* infoOf(const QVector<LayerInfo>& infos, int layer, int datatype)
{
    for (const auto& i : infos) {
        if (!i.isGroup && i.layer == layer && i.datatype == datatype) return &i;
    }
    return nullptr;
}

} // namespace

TEST(AssemblySheet, ComposedLoadGroupsRowsHidesFillAndNotesTheMissingDie)
{
    if (QGuiApplication::platformName() == "offscreen" ||
        QGuiApplication::platformName() == "minimal")
        GTEST_SKIP() << "Requires actual X11/Xvfb display";
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const QString gds = temp.filePath("assembly.gds");
    const QString carrierLyp = temp.filePath("carrier.lyp");
    const QString dieLyp = temp.filePath("die.lyp");
    writeAssembly(gds);
    writeSheet(carrierLyp,
        "<properties><name>TopMetal1</name><source>126/0@1</source>"
        "<fill-color>#12ab34</fill-color><visible>true</visible></properties>"
        "<properties><name>Metal4.filler</name><source>50/22@1</source>"
        "<fill-color>#3456ab</fill-color><visible>true</visible></properties>");
    writeSheet(dieLyp,
        "<properties><name>Metal1</name><source>8/0@1</source>"
        "<fill-color>#ab3412</fill-color><visible>true</visible></properties>"
        "<properties><name>TopMetal1.die</name><source>126/0@1</source>"
        "<fill-color>#ffffff</fill-color><visible>true</visible></properties>");

    LoadSpec spec;
    spec.carrierTechs = {kCarrier};
    spec.sheets = {{kCarrier, carrierLyp.toStdString(), {{50, 22}}},
                   {kDie, dieLyp.toStdString(), {}}};
    spec.dies = {{"U1", kDie}, {"U2", kDie}};

    KLayout2DView view;
    view.resize(800, 600);
    view.show();
    ASSERT_TRUE(view.loadComposedLayout(gds, spec));

    const QVector<LayerInfo> infos = view.layerInfos();
    QStringList headings;
    for (const auto& i : infos) {
        if (i.isGroup) headings << i.name;
    }
    EXPECT_EQ(headings, (QStringList{kCarrier, kSharedGroup, kDie}));

    const LayerInfo* tm1 = infoOf(infos, 126, 0);
    const LayerInfo* fill = infoOf(infos, 50, 22);
    const LayerInfo* m1 = infoOf(infos, 8, 0);
    const LayerInfo* bump = infoOf(infos, 500, 0);
    ASSERT_TRUE(tm1 && fill && m1 && bump);

    EXPECT_EQ(tm1->group, QString(kSharedGroup));
    EXPECT_EQ(tm1->name, "TopMetal1 (shared: intm4tm2, ihp-sg13g2)");
    EXPECT_EQ(tm1->fillColor, QColor("#12ab34"));  // the carrier's style, not the die's

    EXPECT_EQ(m1->group, QString(kDie));
    EXPECT_EQ(m1->fillColor, QColor("#ab3412"));

    EXPECT_EQ(fill->group, QString(kCarrier));
    EXPECT_TRUE(fill->fill);
    EXPECT_FALSE(fill->visible);

    EXPECT_EQ(bump->group, QString(kCarrier));
    EXPECT_TRUE(bump->name.contains("default styling"));

    EXPECT_TRUE(view.hasFill());
    view.setFillVisible(true);
    EXPECT_TRUE(infoOf(view.layerInfos(), 50, 22)->visible);
    EXPECT_TRUE(infoOf(view.layerInfos(), 126, 0)->visible);
    view.setFillVisible(false);
    EXPECT_FALSE(infoOf(view.layerInfos(), 50, 22)->visible);

    ASSERT_EQ(view.notes().size(), 1);
    EXPECT_EQ(view.notes().front(),
              "2D: no cell in assembly.gds is named U2_..., so whatever U2 draws there "
              "is listed with the carrier.");
}

// -- The carrier fill an assembled GDS leaves out, read from the interposer's
//    own GDS on the first Show Fill --

namespace {

const LayerNum kBoundary{235, 0};

// The assembled GDS: carrier routing and outline in the top cell, die U1 with
// its own fill. `carrierFill` puts carrier fill in the top cell as well.
void writeComplete(const QString& path, bool carrierFill = false)
{
    db::Layout layout;
    layout.dbu(0.001);
    const auto top = layout.add_cell("INTERPOSER");
    const auto u1 = layout.add_cell("U1_layout_flipped");
    const auto tm1 = layout.insert_layer(db::LayerProperties(126, 0));
    const auto boundary = layout.insert_layer(db::LayerProperties(235, 0));
    const auto m4fill = layout.insert_layer(db::LayerProperties(50, 22));
    const auto m1 = layout.insert_layer(db::LayerProperties(8, 0));
    layout.cell(top).shapes(tm1).insert(db::Box(1000, 1000, 99000, 2000));
    layout.cell(top).shapes(boundary).insert(db::Box(0, 0, 100000, 200000));
    if (carrierFill) layout.cell(top).shapes(m4fill).insert(db::Box(5000, 5000, 6000, 6000));
    layout.cell(u1).shapes(m1).insert(db::Box(0, 0, 10000, 10000));
    layout.cell(u1).shapes(m4fill).insert(db::Box(1000, 1000, 2000, 2000));
    layout.cell(top).insert(db::CellInstArray(db::CellInst(u1), db::Trans(db::Vector(40000, 40000))));
    tl::OutputStream stream(path.toStdString());
    db::SaveLayoutOptions options;
    db::Writer(options).write(layout, stream);
}

// The interposer's own GDS: the same carrier, outline shifted by `shift` dbu,
// with 3 + 2 fill shapes in the top cell and 4 more in a child cell.
void writeInterposer(const QString& path, int shift = 0)
{
    db::Layout layout;
    layout.dbu(0.001);
    const auto top = layout.add_cell("INTERPOSER");
    const auto tile = layout.add_cell("FILL_TILE");
    const auto tm1 = layout.insert_layer(db::LayerProperties(126, 0));
    const auto boundary = layout.insert_layer(db::LayerProperties(235, 0));
    const auto m4fill = layout.insert_layer(db::LayerProperties(50, 22));
    const auto m1fill = layout.insert_layer(db::LayerProperties(67, 22));
    layout.cell(top).shapes(tm1).insert(db::Box(1000, 1000, 99000, 2000));
    layout.cell(top).shapes(boundary).insert(db::Box(shift, 0, 100000 + shift, 200000));
    for (int i = 0; i < 3; ++i) {
        layout.cell(top).shapes(m4fill).insert(db::Box(10000 * i, 50000, 10000 * i + 2000, 52000));
    }
    for (int i = 0; i < 2; ++i) {
        layout.cell(top).shapes(m1fill).insert(db::Box(10000 * i, 60000, 10000 * i + 2000, 62000));
    }
    layout.cell(tile).shapes(m1fill).insert(db::Box(0, 0, 1000, 1000));
    layout.cell(top).insert(db::CellInstArray(db::CellInst(tile), db::Trans(db::Vector(0, 100000)),
                                              db::Vector(2000, 0), db::Vector(0, 2000), 2, 2));
    tl::OutputStream stream(path.toStdString());
    db::SaveLayoutOptions options;
    db::Writer(options).write(layout, stream);
}

const char kCarrierSheet[] =
    "<properties><name>TopMetal1</name><source>126/0@1</source>"
    "<fill-color>#12ab34</fill-color><visible>true</visible></properties>"
    "<properties><name>Metal4.filler</name><source>50/22@1</source>"
    "<fill-color>#3456ab</fill-color><visible>true</visible></properties>"
    "<properties><name>Metal1.filler</name><source>67/22@1</source>"
    "<fill-color>#5634ab</fill-color><visible>true</visible></properties>"
    "<properties><name>prBoundary.drawing</name><source>235/0@1</source>"
    "<fill-color>#000000</fill-color><visible>true</visible></properties>";

const char kDieSheet[] =
    "<properties><name>Metal1</name><source>8/0@1</source>"
    "<fill-color>#ab3412</fill-color><visible>true</visible></properties>"
    "<properties><name>Metal4.filler.die</name><source>50/22@1</source>"
    "<fill-color>#ab5634</fill-color><visible>true</visible></properties>";

class CarrierFill2D : public ::testing::Test {
protected:
    void SetUp() override
    {
        if (QGuiApplication::platformName() == "offscreen" ||
            QGuiApplication::platformName() == "minimal")
            GTEST_SKIP() << "Requires actual X11/Xvfb display";
        ASSERT_TRUE(temp.isValid());
        complete = temp.filePath("demo_complete.gds");
        interposer = temp.filePath("demo_interposer.gds");
        carrierLyp = temp.filePath("carrier.lyp");
        dieLyp = temp.filePath("die.lyp");
        writeSheet(dieLyp, kDieSheet);
    }

    LoadSpec spec() const
    {
        LoadSpec s;
        s.carrierTechs = {kCarrier};
        s.sheets = {{kCarrier, carrierLyp.toStdString(), {{50, 22}, {67, 22}}},
                    {kDie, dieLyp.toStdString(), {{50, 22}}}};
        s.dies = {{"U1", kDie}};
        s.carrierFillPath = interposer.toStdString();
        return s;
    }

    // The carrier fill's rows, under their own heading (the die's own 50/22 is
    // listed with the die).
    static QVector<LayerInfo> carrierFillRows(const KLayout2DView& view)
    {
        QVector<LayerInfo> rows;
        for (const auto& i : view.layerInfos()) {
            if (!i.isGroup && i.fill && i.group == QString(kCarrier) + " (carrier fill)") rows << i;
        }
        return rows;
    }

    // What places a row in the list: KLayout offsets a layer's stipple by its
    // place, so a row that moves draws its hatch shifted.
    static QStringList listOrder(const KLayout2DView& view)
    {
        QStringList order;
        for (const auto& i : view.layerInfos()) {
            order << QString("%1|%2|%3/%4|%5").arg(i.group, i.name).arg(i.layer).arg(i.datatype).arg(i.isGroup);
        }
        return order;
    }

    static bool notesSay(const KLayout2DView& view, const QString& text)
    {
        for (const auto& n : view.notes()) {
            if (n.contains(text)) return true;
        }
        return false;
    }

    QTemporaryDir temp;
    QString complete, interposer, carrierLyp, dieLyp;
};

} // namespace

TEST_F(CarrierFill2D, IsReadOnTheFirstShowFillAndFollowsTheSwitch)
{
    writeComplete(complete);
    writeInterposer(interposer);
    writeSheet(carrierLyp, kCarrierSheet);

    KLayout2DView view;
    view.resize(800, 600);
    view.show();
    ASSERT_TRUE(view.loadComposedLayout(complete, spec()));
    const QStringList cells = view.cellNames();

    // Nothing read at open: only the die's fill is listed.
    EXPECT_TRUE(carrierFillRows(view).isEmpty());
    const LayerInfo* dieFill = infoOf(view.layerInfos(), 50, 22);
    ASSERT_TRUE(dieFill);
    EXPECT_EQ(dieFill->group, QString(kDie));
    const QStringList before = listOrder(view);

    view.setFillVisible(true);
    QVector<LayerInfo> rows = carrierFillRows(view);
    ASSERT_EQ(rows.size(), 2);
    // Appended after every row there was, none of which moves.
    const QStringList after = listOrder(view);
    ASSERT_EQ(after.size(), before.size() + 3);  // a heading and two rows
    EXPECT_EQ(after.mid(0, before.size()), before);
    EXPECT_EQ(rows[0].layer, 50);
    EXPECT_EQ(rows[0].name, "Metal4.filler (fill)");
    EXPECT_EQ(rows[0].fillColor, QColor("#3456ab"));  // the carrier sheet's style
    EXPECT_EQ(rows[1].layer, 67);
    for (const auto& r : rows) EXPECT_TRUE(r.visible);
    EXPECT_FALSE(notesSay(view, "carrier fill not drawn"));

    // The shown layout stays the active one: same cells, same top.
    EXPECT_EQ(view.cellNames(), cells);
    EXPECT_EQ(view.currentCellName(), "INTERPOSER");

    view.setFillVisible(false);
    for (const auto& r : carrierFillRows(view)) EXPECT_FALSE(r.visible);
    EXPECT_FALSE(infoOf(view.layerInfos(), 50, 22)->visible);

    // Read once: switching on again adds no rows.
    const int count = view.layerCount();
    view.setFillVisible(true);
    EXPECT_EQ(view.layerCount(), count);
    for (const auto& r : carrierFillRows(view)) EXPECT_TRUE(r.visible);

    // A reload forgets it until the next Show Fill.
    ASSERT_TRUE(view.loadComposedLayout(complete, spec()));
    EXPECT_TRUE(carrierFillRows(view).isEmpty());
}

TEST_F(CarrierFill2D, AnotherFrameIsNotDrawnAndSaysSo)
{
    writeComplete(complete);
    writeInterposer(interposer, 5);  // the outline 5 dbu off
    writeSheet(carrierLyp, kCarrierSheet);

    KLayout2DView view;
    view.resize(800, 600);
    view.show();
    ASSERT_TRUE(view.loadComposedLayout(complete, spec()));
    const int count = view.layerCount();
    view.setFillVisible(true);
    EXPECT_TRUE(carrierFillRows(view).isEmpty());
    EXPECT_EQ(view.layerCount(), count);
    EXPECT_TRUE(notesSay(view, "2D: carrier fill not drawn: demo_interposer.gds and "
                               "demo_complete.gds do not share the carrier frame"));
}

TEST_F(CarrierFill2D, NoOutlineToCheckIsNotDrawnAndSaysSo)
{
    writeComplete(complete);
    writeInterposer(interposer);
    writeSheet(carrierLyp,
        "<properties><name>Metal4.filler</name><source>50/22@1</source>"
        "<fill-color>#3456ab</fill-color><visible>true</visible></properties>");

    KLayout2DView view;
    view.resize(800, 600);
    view.show();
    ASSERT_TRUE(view.loadComposedLayout(complete, spec()));
    view.setFillVisible(true);
    EXPECT_TRUE(carrierFillRows(view).isEmpty());
    EXPECT_TRUE(notesSay(view, "no carrier sheet names a prBoundary layer"));
}

TEST_F(CarrierFill2D, ALayoutThatDrawsTheCarrierFillIsNotOverlaid)
{
    writeComplete(complete, true);
    writeInterposer(interposer);
    writeSheet(carrierLyp, kCarrierSheet);

    KLayout2DView view;
    view.resize(800, 600);
    view.show();
    ASSERT_TRUE(view.loadComposedLayout(complete, spec()));
    const int count = view.layerCount();
    view.setFillVisible(true);
    EXPECT_EQ(view.layerCount(), count);
    EXPECT_FALSE(notesSay(view, "carrier fill not drawn"));
}

#endif // HAVE_KLAYOUT

TEST(AssemblySheet, CarrierFillSourceIsTheInterposerLayoutWhenItIsNotTheShownOne)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const std::string own = temp.filePath("demo_interposer.gds").toStdString();
    const std::string shown = temp.filePath("demo_complete.gds").toStdString();
    for (const auto& p : {own, shown}) {
        QFile f(QString::fromStdString(p));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    }
    Assembly assembly;
    auto carrier = std::make_unique<Component>("interposer", ComponentType::Interposer);
    carrier->set_layout_path(own);
    assembly.add_component(std::move(carrier));

    EXPECT_EQ(carrierFillSource(assembly, shown), own);
    EXPECT_EQ(carrierFillSource(assembly, own), "");  // the shown layout is the interposer's
    QFile::remove(QString::fromStdString(own));
    EXPECT_EQ(carrierFillSource(assembly, shown), "");  // nothing to read
}
