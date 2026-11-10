// Real embedded KLayout regression: hierarchical geometry and partial palettes.
#include <gtest/gtest.h>
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <stdexcept>
#include "view2d/KLayout2DView.h"

#ifdef HAVE_KLAYOUT
#include "dbLayout.h"
#include "dbCell.h"
#include "dbCellInst.h"
#include "dbBox.h"
#include "dbLayerProperties.h"
#include "dbWriter.h"
#include "tlStream.h"
#endif

namespace chiplet {
#ifdef HAVE_KLAYOUT
namespace {
void writeHierarchy(const QString& path) {
    db::Layout layout;
    layout.dbu(0.001);
    const auto top = layout.add_cell("INTERPOSER");
    const auto inner = layout.add_cell("ROUTING");
    const auto leaf = layout.add_cell("DIE_PROJECTION");
    const auto tm1 = layout.insert_layer(db::LayerProperties(126, 0));
    const auto tm2 = layout.insert_layer(db::LayerProperties(134, 0));
    const auto pads = layout.insert_layer(db::LayerProperties(205, 0));
    const auto outline = layout.insert_layer(db::LayerProperties(206, 0));
    layout.cell(top).shapes(tm1).insert(db::Box(0, 0, 200000, 10000));
    layout.cell(inner).shapes(tm2).insert(db::Box(0, 0, 10000, 200000));
    layout.cell(leaf).shapes(pads).insert(db::Box(20000, 20000, 50000, 50000));
    layout.cell(leaf).shapes(outline).insert(db::Box(10000, 10000, 80000, 80000));
    layout.cell(inner).insert(db::CellInstArray(db::CellInst(leaf), db::Trans()));
    layout.cell(top).insert(db::CellInstArray(db::CellInst(inner), db::Trans()));
    tl::OutputStream stream(path.toStdString());
    db::SaveLayoutOptions options;
    db::Writer writer(options);
    writer.write(layout, stream);
}

void writePalette(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) throw std::runtime_error("palette write failed");
    file.write("<?xml version=\"1.0\"?><layer-properties>"
        "<properties><name>Carrier TM2</name><source>134/0@1</source>"
        "<fill-color>#12ab34</fill-color><frame-color>#12ab34</frame-color><visible>true</visible></properties>"
        "<properties><name>Hidden TM1</name><source>126/0@1</source>"
        "<fill-color>#ab1234</fill-color><visible>false</visible></properties>"
        "</layer-properties>");
}
}
#endif

TEST(Coupon2DVisibility, HierarchicalCarrierPreservesPaletteAndAddsProjectionLayers) {
#ifndef HAVE_KLAYOUT
    GTEST_SKIP() << "Requires real KLayout integration";
#else
    if (QGuiApplication::platformName() == "offscreen" || QGuiApplication::platformName() == "minimal")
        GTEST_SKIP() << "Requires actual X11/Xvfb display";
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto gds = temp.filePath("hierarchy.gds"), lyp = temp.filePath("carrier.lyp");
    writeHierarchy(gds); writePalette(lyp);
    KLayout2DView view;
    view.resize(800, 600); view.show();
    ASSERT_TRUE(view.loadLayout(gds, lyp));
    ASSERT_TRUE(view.isViewAvailable());
    ASSERT_EQ(view.currentCellName(), "INTERPOSER");
    EXPECT_EQ(view.currentCellHierarchyDepth(), 2);
    EXPECT_EQ(view.maxHierLevels(), 3);
    bool carrier = false, hidden = false, projection = false, outline = false;
    for (const auto& layer : view.layerInfos()) {
        if (layer.layer == 134 && layer.datatype == 0) {
            carrier = true; EXPECT_EQ(layer.name, "Carrier TM2");
            EXPECT_EQ(layer.fillColor, QColor("#12ab34")); EXPECT_TRUE(layer.visible);
        }
        if (layer.layer == 126 && layer.datatype == 0) {
            hidden = true; EXPECT_FALSE(layer.visible); EXPECT_EQ(layer.name, "Hidden TM1");
        }
        if (layer.layer == 205 && layer.datatype == 0) {
            projection = true; EXPECT_TRUE(layer.visible); EXPECT_TRUE(layer.name.contains("default styling"));
        }
        if (layer.layer == 206 && layer.datatype == 0) {
            outline = true; EXPECT_TRUE(layer.visible);
        }
    }
    EXPECT_TRUE(carrier && hidden && projection && outline);
#endif
}

TEST(Coupon2DVisibility, SelectingLeafAndReturningToCarrierRestoresFullContentDepth) {
#ifndef HAVE_KLAYOUT
    GTEST_SKIP() << "Requires real KLayout integration";
#else
    if (QGuiApplication::platformName() == "offscreen" || QGuiApplication::platformName() == "minimal")
        GTEST_SKIP() << "Requires actual X11/Xvfb display";
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto gds = temp.filePath("hierarchy.gds");
    writeHierarchy(gds);
    KLayout2DView view;
    view.resize(800, 600); view.show();
    ASSERT_TRUE(view.loadLayout(gds));
    view.setMaxHierLevels(0);
    ASSERT_TRUE(view.setCurrentCell("DIE_PROJECTION"));
    EXPECT_EQ(view.currentCellHierarchyDepth(), 0);
    EXPECT_EQ(view.maxHierLevels(), 1);
    view.setMaxHierLevels(0);
    ASSERT_TRUE(view.setCurrentCell("INTERPOSER"));
    EXPECT_EQ(view.currentCellHierarchyDepth(), 2);
    EXPECT_EQ(view.maxHierLevels(), 3);
#endif
}
} // namespace chiplet
