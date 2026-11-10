// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * KLayout2DView.cpp - Implementation of KLayout 2D view wrapper
 *
 * Uses lazy initialization to avoid crashes in headless environments.
 * The LayoutViewWidget is only created when actually needed and display is available.
 */

#include "KLayout2DView.h"
#include "EmbeddedDispatcher.h"

#include <QLabel>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>

#ifdef HAVE_KLAYOUT
#include "layLayoutView.h"
#include "layLayerProperties.h"
#include "layParsedLayerSource.h"
#include "layCellView.h"
#include "dbLayout.h"
#include "dbBox.h"
#include "dbPoint.h"
#include "dbLoadLayoutOptions.h"
#include "dbCommonReader.h"
#include "dbRecursiveShapeIterator.h"
#include "dbStreamLayers.h"
#include "tlColor.h"
#include "tlException.h"
#include "tlObject.h"
#include <algorithm>
#include <cmath>
#include <set>
#endif

namespace chiplet {

#ifdef HAVE_KLAYOUT
class KLayout2DView::CellViewEventBridge : public tl::Object {
public:
    CellViewEventBridge(KLayout2DView* parent) : m_parent(parent) {}
    void on_cellview_changed(int /*index*/) {
        if (m_parent->m_blockCellNavigation) return;
        QString name = m_parent->currentCellName();
        if (!name.isEmpty()) {
            emit m_parent->cellNavigated(name);
        }
    }
private:
    KLayout2DView* m_parent;
};

// Bridges KLayout's viewport_changed_event (a no-arg tl::Event) to the Qt
// viewportChanged() signal. Same lifetime rule as CellViewEventBridge: reset
// before the view widget so the event subscription never dangles.
class KLayout2DView::ViewportEventBridge : public tl::Object {
public:
    ViewportEventBridge(KLayout2DView* parent) : m_parent(parent) {}
    void on_viewport_changed() {
        emit m_parent->viewportChanged();
    }
private:
    KLayout2DView* m_parent;
};
#endif

// The carrier fill of a composed load: where to read it, the rows it gets,
// and whether the read was tried. Rows are styled from the carrier sheet at
// load time, since the sheets are read then.
struct KLayout2DView::CarrierFill {
    std::string path;
    LayerNum boundary{-1, -1};  // the carrier sheet's prBoundary; checks the frame
    struct Row {
        LayerNum key;
        std::string group;
        std::string name;
#ifdef HAVE_KLAYOUT
        lay::LayerPropertiesNode node;
        bool styled = false;
#endif
    };
    std::vector<Row> rows;
    bool tried = false;
};

namespace {

#ifdef HAVE_KLAYOUT
int cellviewOf(const lay::ParsedLayerSource& src)
{
    // No index means the first cellview (LayerProperties::do_realize).
    return src.cv_index() < 0 ? 0 : src.cv_index();
}

// The single top cell, or none when there are several or none.
bool soleTop(const db::Layout& layout, db::cell_index_type& top)
{
    int count = 0;
    for (auto t = layout.begin_top_down(); t != layout.end_top_cells(); ++t) {
        top = *t;
        ++count;
    }
    return count == 1;
}

// The box of `cell`'s own shapes on one layer; empty if the layer is absent.
db::Box ownBox(const db::Layout& layout, db::cell_index_type cell, const LayerNum& key)
{
    for (auto l = layout.begin_layers(); l != layout.end_layers(); ++l) {
        const db::LayerProperties& p = *(*l).second;
        if (p.layer == key.first && p.datatype == key.second) {
            return layout.cell(cell).shapes((*l).first).bbox();
        }
    }
    return db::Box();
}

long peakResidentMiB()
{
    QFile status("/proc/self/status");
    if (!status.open(QIODevice::ReadOnly)) return -1;
    for (const QByteArray& line : status.readAll().split('\n')) {
        if (line.startsWith("VmHWM:")) {
            return line.mid(6).trimmed().split(' ').first().toLong() / 1024;
        }
    }
    return -1;
}
#endif

} // namespace

KLayout2DView::KLayout2DView(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

KLayout2DView::~KLayout2DView()
{
#ifdef HAVE_KLAYOUT
    // Destroy bridges before view widget to avoid dangling event subscriptions
    m_cellViewBridge.reset();
    m_viewportBridge.reset();
#endif
}

bool KLayout2DView::isDisplayAvailable()
{
    // Check if we have a valid display/screen
    QGuiApplication* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (!app) {
        return false;
    }

    // Check for offscreen platform (headless)
    QString platform = app->platformName();
    if (platform == "offscreen" || platform == "minimal") {
        return false;
    }

    // Check if we have at least one screen
    QList<QScreen*> screens = app->screens();
    return !screens.isEmpty();
}

bool KLayout2DView::ensureViewWidget()
{
#ifdef HAVE_KLAYOUT
    if (m_initAttempted) {
        return m_viewAvailable;
    }
    m_initAttempted = true;

    if (!isDisplayAvailable()) {
        qWarning("KLayout2DView: No display available for 2D view");
        m_viewAvailable = false;
        return false;
    }

    try {
        // Create dispatcher with menu infrastructure BEFORE creating LayoutViewWidget.
        // This is critical: plugins call dispatcher->menu() during init, which crashes
        // if the menu hasn't been created via make_menu().
        m_dispatcher = std::make_unique<EmbeddedDispatcher>(this);

        if (!m_dispatcher->isInitialized()) {
            qWarning("KLayout2DView: Failed to initialize dispatcher");
            m_viewAvailable = false;
            return false;
        }

        // Remove placeholder widget
        if (m_layout->count() > 0) {
            QWidget* placeholder = m_layout->itemAt(0)->widget();
            if (placeholder) {
                m_layout->removeWidget(placeholder);
                delete placeholder;
            }
        }

        // Now safe to create LayoutViewWidget - dispatcher menu is ready
        // Constructor: (db::Manager*, bool editable, lay::Plugin*, QWidget*, unsigned int options)
        m_viewWidget = new lay::LayoutViewWidget(
            nullptr,           // db::Manager - not needed for viewing
            false,             // editable = false (view-only mode)
            m_dispatcher.get(),
            this
            // LV_Normal options is the default
        );

        m_layout->addWidget(m_viewWidget);
        m_viewAvailable = true;

        connectSignals();

        qDebug() << "KLayout2DView: LayoutViewWidget created successfully";
        return true;

    } catch (const tl::Exception& e) {
        qCritical() << "KLayout2DView: Failed to create widget:" << e.msg().c_str();
        m_viewAvailable = false;
        return false;
    } catch (const std::exception& e) {
        qCritical() << "KLayout2DView: Failed to create widget:" << e.what();
        m_viewAvailable = false;
        return false;
    }
#else
    return false;
#endif
}

void KLayout2DView::setupUI()
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);

    // Always start with a placeholder - real widget created lazily
    QLabel* placeholder = new QLabel(this);
#ifdef HAVE_KLAYOUT
    placeholder->setText("2D View (double-click component to load)");
#else
    placeholder->setText("2D View (KLayout not available)");
#endif
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet("background-color: #2a2a2a; color: #888;");
    m_layout->addWidget(placeholder);
}

void KLayout2DView::connectSignals()
{
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget) return;

    // Connect position tracking signal
    connect(m_viewWidget, &lay::LayoutViewWidget::current_pos_changed,
            this, [this](double x, double y, bool /*dbu_units*/) {
                emit positionChanged(x, y);
            });

    // Subscribe to cellview_changed_event for 2D->3D cell navigation
    lay::LayoutView* view = m_viewWidget->view();
    if (view) {
        m_cellViewBridge = std::make_unique<CellViewEventBridge>(this);
        view->cellview_changed_event.add(m_cellViewBridge.get(),
            &CellViewEventBridge::on_cellview_changed);

        // Subscribe to viewport_changed_event so the overview navigator can
        // track zoom/pan.
        m_viewportBridge = std::make_unique<ViewportEventBridge>(this);
        view->viewport_changed_event.add(m_viewportBridge.get(),
            &ViewportEventBridge::on_viewport_changed);
    }
#endif
}

bool KLayout2DView::loadLayout(const QString& path, const QString& lypPath)
{
#ifdef HAVE_KLAYOUT
    // Lazy init: create widget on first use
    if (!ensureViewWidget()) {
        qWarning("KLayout2DView::loadLayout: View not available");
        return false;
    }

    lay::LayoutView* view = m_viewWidget->view();
    if (!view) return false;
    m_notes.clear();
    m_rowMeta.clear();
    m_carrierFill.reset();

    try {
        // Block cellNavigated during load to prevent spurious events
        m_blockCellNavigation = true;

        // Clear existing layouts
        while (view->cellviews() > 0) {
            view->erase_cellview(0);
        }

        // Load the new layout
        db::LoadLayoutOptions options;
        view->load_layout(path.toStdString(), options, true /*add_cellview*/);

        // Load layer properties if provided. A .lyp parse/IO failure must NOT
        // fail the whole load: the GDS is already loaded, so degrade to the
        // default auto-generated layers instead of returning false (which left
        // the caller thinking nothing loaded).
        bool useDefaultLayers = lypPath.isEmpty();
        if (!lypPath.isEmpty()) {
            try {
                view->load_layer_props(lypPath.toStdString());
            } catch (const tl::Exception& e) {
                qWarning("KLayout2DView::loadLayout: failed to load layer props '%s': %s; "
                         "falling back to default layers",
                         qPrintable(lypPath), e.msg().c_str());
                useDefaultLayers = true;
            }
        }
        if (useDefaultLayers) {
            // Black-box / no-LYP chiplet (commercial / closed PDK node): KLayout
            // auto-creates a visible node for every detected GDS layer, so the
            // metal pads render with default colors. Guarantee the pad-name text
            // shows -- KLayout draws GDS TEXT records natively -- with a
            // high-contrast color and no lazy drop on small pads.
            view->text_visible(true);
            view->text_lazy_rendering(false);
            view->text_color(tl::Color(255, 255, 255));
        }

        // Preserve palette styles/visibility; add default-styled entries only
        // for actual geometry layers it omits, such as die projections.
        std::set<std::string> paletteSources;
        for (auto it = view->begin_layers(); !it.at_end(); ++it) {
            if (!it->has_children()) paletteSources.insert(it->source_string(true));
        }
        view->add_missing_layers();
        for (auto it = view->begin_layers(); !it.at_end(); ++it) {
            if (!it->has_children() && !paletteSources.count(it->source_string(true))) {
                lay::LayerPropertiesNode props(*it);
                props.set_name(it->source_string(true) + " (default styling)");
                view->replace_layer_node(it, props);
            }
        }
        // Embedded views bypass KLayout MainWindow's initial-depth setup.
        // Level zero shows frames; level one includes the cell's own shapes.
        const auto& loaded = view->cellview(0);
        if (loaded.is_valid() && loaded.cell()) {
            view->set_hier_levels(std::make_pair(0, int(loaded.cell()->hierarchy_levels()) + 1));
        }

        // Zoom to fit
        view->zoom_fit();

        m_blockCellNavigation = false;
        m_currentPath = path;
        emit layoutChanged(true);
        return true;

    } catch (const tl::Exception& e) {
        m_blockCellNavigation = false;
        qWarning("KLayout2DView::loadLayout failed: %s", e.msg().c_str());
        m_currentPath.clear();
        emit layoutChanged(false);
        return false;
    } catch (const std::exception& e) {
        m_blockCellNavigation = false;
        qWarning("KLayout2DView::loadLayout failed: %s", e.what());
        m_currentPath.clear();
        emit layoutChanged(false);
        return false;
    }
#else
    Q_UNUSED(path);
    Q_UNUSED(lypPath);
    return false;
#endif
}

bool KLayout2DView::loadComposedLayout(const QString& path, const LoadSpec& spec)
{
#ifdef HAVE_KLAYOUT
    m_notes.clear();
    m_rowMeta.clear();
    m_carrierFill.reset();
    if (!ensureViewWidget()) {
        qWarning("KLayout2DView::loadComposedLayout: View not available");
        return false;
    }
    lay::LayoutView* view = m_viewWidget->view();
    if (!view) return false;

    try {
        m_blockCellNavigation = true;
        while (view->cellviews() > 0) {
            view->erase_cellview(0);
        }
        db::LoadLayoutOptions options;
        view->load_layout(path.toStdString(), options, true /*add_cellview*/);

        Occupancy occupancy;
        const lay::CellView& cv = view->cellview(0);
        if (cv.is_valid() && cv.cell()) {
            occupancy = measureOccupancy(cv->layout(), cv.cell_index(), spec.dies);
        }
        const QString file = QFileInfo(path).fileName();
        for (const auto& id : occupancy.unmatchedDies) {
            const QString comp = QString::fromStdString(id);
            m_notes << QString("2D: no cell in %1 is named %2_..., so whatever %2 "
                               "draws there is listed with the carrier.").arg(file, comp);
        }

        // Each sheet is read by KLayout itself, which owns custom dither
        // patterns and line styles: a load merges them into the view's tables
        // and remaps the sheet onto them, so a node harvested here keeps valid
        // indices after the next sheet loads.
        std::map<std::string, SheetTech> sheets;
        std::map<std::string, std::map<LayerNum, lay::LayerPropertiesNode>> styles;
        bool anySheet = false;
        for (const SheetSpec& sheetSpec : spec.sheets) {
            SheetTech& tech = sheets[sheetSpec.techId];
            tech.fill = sheetSpec.fill;
            if (sheetSpec.lypPath.empty()) continue;
            try {
                view->load_layer_props(sheetSpec.lypPath);
            } catch (const tl::Exception& e) {
                m_notes << QString("%1 layer sheet could not be read (%2: %3). 2D uses "
                                   "default styling for its layers.")
                               .arg(QString::fromStdString(sheetSpec.techId),
                                    QString::fromStdString(sheetSpec.lypPath),
                                    QString::fromStdString(e.msg()));
                continue;
            }
            anySheet = true;
            auto& nodes = styles[sheetSpec.techId];
            for (auto it = view->begin_layers(); !it.at_end(); ++it) {
                if (it->has_children()) continue;
                const lay::ParsedLayerSource& src = it->source(true);
                if (src.layer() < 0 || src.datatype() < 0) continue;
                const LayerNum key(src.layer(), src.datatype());
                if (tech.names.count(key)) continue;  // the first entry for a number wins
                tech.order.push_back(key);
                tech.names[key] = it->name();
                tech.visible[key] = it->visible(true);
                nodes.emplace(key, it->flat());
            }
        }

        lay::LayerPropertiesList list;
        list.set_dither_pattern(view->dither_pattern());
        list.set_line_styles(view->line_styles());
        for (const ComposedGroup& group : composeAssemblySheet(occupancy.uses,
                                                               spec.carrierTechs, sheets)) {
            lay::LayerPropertiesNode heading;
            heading.set_name(group.title);
            for (const ComposedRow& row : group.rows) {
                lay::LayerPropertiesNode leaf;
                auto techIt = styles.find(row.styleTech);
                if (techIt != styles.end() && techIt->second.count(row.key)) {
                    leaf = techIt->second.at(row.key);
                } else {
                    lay::LayerProperties props;
                    props.set_source(lay::ParsedLayerSource(row.key.first, row.key.second, 0));
                    view->init_layer_properties(props);
                    leaf = lay::LayerPropertiesNode(props);
                }
                leaf.set_name(row.name);
                leaf.set_visible(row.visible);
                heading.add_child(leaf);
                m_rowMeta[{0, row.key}] = RowMeta{QString::fromStdString(group.title), row.fill};
            }
            list.push_back(heading);
        }
        view->set_properties(list);

        // The carrier fill the shown layout leaves out, read on the first Show
        // Fill. Not when the layout already draws carrier fill: that would be
        // drawn twice.
        if (!spec.carrierFillPath.empty()) {
            std::set<LayerNum> carrierDrawn;
            for (const LayerUse& use : occupancy.uses) {
                if (use.carrier) carrierDrawn.insert(use.key);
            }
            auto fill = std::make_unique<CarrierFill>();
            fill->path = spec.carrierFillPath;
            bool drawn = false;
            for (const auto& techId : spec.carrierTechs) {
                auto sheet = sheets.find(techId);
                if (sheet == sheets.end()) continue;
                for (const auto& entry : sheet->second.names) {
                    if (fill->boundary.first < 0 && entry.second.rfind("prBoundary", 0) == 0) {
                        fill->boundary = entry.first;
                    }
                }
                for (const LayerNum& key : sheet->second.fill) {
                    if (carrierDrawn.count(key)) drawn = true;
                    if (std::any_of(fill->rows.begin(), fill->rows.end(),
                                    [&](const CarrierFill::Row& r) { return r.key == key; })) {
                        continue;
                    }
                    // Styled and listed by the first carrier sheet naming it,
                    // as composeAssemblySheet does for carrier layers.
                    CarrierFill::Row row;
                    row.key = key;
                    row.group = spec.carrierTechs.front();
                    for (const auto& styleTech : spec.carrierTechs) {
                        auto s = sheets.find(styleTech);
                        if (s == sheets.end() || !s->second.names.count(key)) continue;
                        row.group = styleTech;
                        row.name = s->second.names.at(key);
                        auto st = styles.find(styleTech);
                        if (st != styles.end() && st->second.count(key)) {
                            row.node = st->second.at(key);
                            row.styled = true;
                        }
                        break;
                    }
                    fill->rows.push_back(std::move(row));
                }
            }
            if (!drawn && !fill->rows.empty()) {
                std::sort(fill->rows.begin(), fill->rows.end(),
                          [](const CarrierFill::Row& a, const CarrierFill::Row& b) {
                              return a.key < b.key;
                          });
                m_carrierFill = std::move(fill);
            }
        }

        if (!anySheet) {
            // Same as the single-sheet load with no sheet: pad names are GDS
            // text, and they are often all a black-box die has to say.
            view->text_visible(true);
            view->text_lazy_rendering(false);
            view->text_color(tl::Color(255, 255, 255));
        }

        const auto& loaded = view->cellview(0);
        if (loaded.is_valid() && loaded.cell()) {
            view->set_hier_levels(std::make_pair(0, int(loaded.cell()->hierarchy_levels()) + 1));
        }
        view->zoom_fit();

        m_blockCellNavigation = false;
        m_currentPath = path;
        emit layoutChanged(true);
        return true;

    } catch (const tl::Exception& e) {
        m_blockCellNavigation = false;
        qWarning("KLayout2DView::loadComposedLayout failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        m_blockCellNavigation = false;
        qWarning("KLayout2DView::loadComposedLayout failed: %s", e.what());
    }
    m_currentPath.clear();
    emit layoutChanged(false);
    return false;
#else
    Q_UNUSED(path);
    Q_UNUSED(spec);
    return false;
#endif
}

void KLayout2DView::setFillVisible(bool visible)
{
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget || !m_viewWidget->view()) {
        return;
    }
    try {
        lay::LayoutView* view = m_viewWidget->view();
        if (visible && m_carrierFill && !m_carrierFill->tried) {
            addCarrierFill();
        }
        for (auto it = view->begin_layers(); !it.at_end(); ++it) {
            if (it->has_children()) continue;
            const lay::ParsedLayerSource& src = it->source(true);
            auto meta = m_rowMeta.find({cellviewOf(src), LayerNum(src.layer(), src.datatype())});
            if (meta == m_rowMeta.end() || !meta->second.fill) continue;
            lay::LayerPropertiesNode props(*it);
            props.set_visible(visible);
            view->replace_layer_node(it, props);
        }
    } catch (const tl::Exception& e) {
        qWarning("KLayout2DView::setFillVisible failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        qWarning("KLayout2DView::setFillVisible failed: %s", e.what());
    }
#else
    Q_UNUSED(visible);
#endif
}

bool KLayout2DView::addCarrierFill()
{
#ifdef HAVE_KLAYOUT
    CarrierFill& fill = *m_carrierFill;
    fill.tried = true;
    lay::LayoutView* view = m_viewWidget ? m_viewWidget->view() : nullptr;
    if (!view || view->cellviews() == 0) return false;

    const QString file = QFileInfo(QString::fromStdString(fill.path)).fileName();
    const QString shown = QFileInfo(m_currentPath).fileName();
    auto refuse = [&](const QString& why) {
        m_notes << QString("2D: carrier fill not drawn: %1").arg(why);
        qInfo("2D: carrier fill not drawn: %s", qPrintable(why));
        emit notesChanged();
        return false;
    };
    if (fill.boundary.first < 0) {
        return refuse(QString("no carrier sheet names a prBoundary layer to check that %1 "
                              "and %2 share the carrier frame").arg(file, shown));
    }

    QElapsedTimer timer;
    timer.start();
    db::LoadLayoutOptions options;
    auto& common = options.get_options<db::CommonReaderOptions>();
    common.create_other_layers = false;
    common.enable_text_objects = false;
    common.enable_properties = false;
    unsigned int logical = 0;
    for (const auto& row : fill.rows) {
        common.layer_map.map(db::LDPair(row.key.first, row.key.second), logical++);
    }
    common.layer_map.map(db::LDPair(fill.boundary.first, fill.boundary.second), logical++);

    // Owned here until add_layout hands it to a cellview.
    auto* handle = new lay::LayoutHandle(new db::Layout(false), fill.path);
    handle->add_ref();
    auto release = [&]() {
        if (handle) handle->remove_ref();
        handle = nullptr;
    };
    try {
        handle->load(options, std::string());
    } catch (const tl::Exception& e) {
        release();
        return refuse(QString("%1 could not be read (%2)").arg(file, QString::fromStdString(e.msg())));
    } catch (const std::exception& e) {
        release();
        return refuse(QString("%1 could not be read (%2)").arg(file, e.what()));
    }

    // Same frame: one top cell each, the same name and dbu, and the same
    // carrier outline in each top cell. A bounding box is no test, since a die
    // overhanging the outline widens the shown layout.
    const db::Layout& shownLayout = view->cellview(0)->layout();
    const db::Layout& read = handle->layout();
    db::cell_index_type shownTop = 0, readTop = 0;
    const bool tops = soleTop(shownLayout, shownTop) && soleTop(read, readTop);
    const db::Box shownBoundary = tops ? ownBox(shownLayout, shownTop, fill.boundary) : db::Box();
    const db::Box readBoundary = tops ? ownBox(read, readTop, fill.boundary) : db::Box();
    if (!tops || std::string(shownLayout.cell_name(shownTop)) != read.cell_name(readTop)
        || std::fabs(shownLayout.dbu() - read.dbu()) > 1e-12
        || shownBoundary.empty() || shownBoundary != readBoundary) {
        release();
        return refuse(QString("%1 and %2 do not share the carrier frame (top cell, dbu or "
                              "%3/%4 outline differ)")
                          .arg(file, shown).arg(fill.boundary.first).arg(fill.boundary.second));
    }

    size_t shapes = 0;
    for (auto l = read.begin_layers(); l != read.end_layers(); ++l) {
        const db::LayerProperties& p = *(*l).second;
        if (LayerNum(p.layer, p.datatype) == fill.boundary) continue;
        for (db::RecursiveShapeIterator it(read, read.cell(readTop), (*l).first); !it.at_end(); ++it) {
            ++shapes;
        }
    }

    // add_layout makes the new cellview active and zooms to fit; the shown
    // layout stays the active one and the view stays where it was.
    const db::DBox box = view->box();
    const std::pair<int, int> levels = view->get_hier_levels();
    const int active = view->active_cellview_index();
    lay::LayerPropertiesList list = view->get_properties();
    const bool wasBlocked = m_blockCellNavigation;
    m_blockCellNavigation = true;
    int cv = -1;
    try {
        cv = int(view->add_layout(handle, true /*add_cellview*/, false /*initialize_layers*/));
        release();  // the cellview holds its own reference now
        view->set_active_cellview_index(active);
        view->zoom_box_and_set_hier_levels(box, levels);
    } catch (const tl::Exception& e) {
        m_blockCellNavigation = wasBlocked;
        release();
        return refuse(QString("%1 could not be shown (%2)").arg(file, QString::fromStdString(e.msg())));
    }
    m_blockCellNavigation = wasBlocked;

    // The rows go after every existing row, under their own heading. KLayout
    // offsets each layer's stipple by its place in the list (stipple-offset),
    // so a row inserted earlier would shift the hatch of every layer after it,
    // for as long as the row stays in the list, hidden or not.
    std::vector<lay::LayerPropertiesNode> headings;
    for (const auto& row : fill.rows) {
        lay::LayerPropertiesNode leaf;
        if (row.styled) {
            leaf = row.node;
        } else {
            lay::LayerProperties props;
            props.set_source(lay::ParsedLayerSource(row.key.first, row.key.second, cv));
            view->init_layer_properties(props);
            leaf = lay::LayerPropertiesNode(props);
        }
        lay::ParsedLayerSource src(row.key.first, row.key.second, cv);
        leaf.set_source(src);
        leaf.set_name((row.name.empty() ? std::string("(default styling)") : row.name) + " (fill)");
        leaf.set_visible(true);
        const std::string group = row.group + " (carrier fill)";
        auto heading = headings.begin();
        for (; heading != headings.end(); ++heading) {
            if (heading->name() == group) break;
        }
        if (heading == headings.end()) {
            headings.emplace_back();
            heading = headings.end() - 1;
            heading->set_name(group);
        }
        heading->add_child(leaf);
        m_rowMeta[{cv, row.key}] = RowMeta{QString::fromStdString(group), true};
    }
    for (const auto& heading : headings) {
        list.push_back(heading);
    }
    view->set_properties(list);

    qInfo("2D: carrier fill read from %s: %zu shapes, %lld ms, VmHWM %ld MiB",
          qPrintable(file), shapes, static_cast<long long>(timer.elapsed()), peakResidentMiB());
    return true;
#else
    return false;
#endif
}

bool KLayout2DView::hasFill() const
{
    for (const auto& entry : m_rowMeta) {
        if (entry.second.fill) return true;
    }
    return false;
}

void KLayout2DView::clearLayout()
{
#ifdef HAVE_KLAYOUT
    if (m_viewWidget && m_viewWidget->view()) {
        lay::LayoutView* view = m_viewWidget->view();
        while (view->cellviews() > 0) {
            view->erase_cellview(0);
        }
    }
#endif
    m_currentPath.clear();
    m_notes.clear();
    m_rowMeta.clear();
    m_carrierFill.reset();
    emit layoutChanged(false);
}

bool KLayout2DView::hasLayout() const
{
#ifdef HAVE_KLAYOUT
    if (m_viewWidget && m_viewWidget->view()) {
        return m_viewWidget->view()->cellviews() > 0;
    }
#endif
    return false;
}

void KLayout2DView::zoomFit()
{
#ifdef HAVE_KLAYOUT
    if (m_viewWidget && m_viewWidget->view()) {
        m_viewWidget->view()->zoom_fit();
    }
#endif
}

int KLayout2DView::maxHierLevels() const
{
#ifdef HAVE_KLAYOUT
    if (m_viewWidget && m_viewWidget->view()) {
        return m_viewWidget->view()->get_max_hier_levels();
    }
#endif
    return -1;
}

int KLayout2DView::currentCellHierarchyDepth() const
{
#ifdef HAVE_KLAYOUT
    if (m_viewWidget && m_viewWidget->view()) {
        try {
            auto* view = m_viewWidget->view();
            if (view->cellviews() > 0) {
                const auto& cv = view->cellview(0);
                if (cv.is_valid() && cv.cell()) return int(cv.cell()->hierarchy_levels());
            }
        } catch (const tl::Exception& e) {
            qWarning("KLayout2DView::currentCellHierarchyDepth failed: %s", e.msg().c_str());
        } catch (const std::exception& e) {
            qWarning("KLayout2DView::currentCellHierarchyDepth failed: %s", e.what());
        }
    }
#endif
    return -1;
}

void KLayout2DView::setMaxHierLevels(int levels)
{
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget || !m_viewWidget->view()) {
        return;
    }
    if (levels < 0) {
        levels = 0;
    }
    lay::LayoutView* view = m_viewWidget->view();
    try {
        view->set_hier_levels(std::make_pair(view->get_min_hier_levels(), levels));
    } catch (const tl::Exception& e) {
        qWarning("KLayout2DView::setMaxHierLevels failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        qWarning("KLayout2DView::setMaxHierLevels failed: %s", e.what());
    }
#else
    Q_UNUSED(levels);
#endif
}

QWidget* KLayout2DView::layerControlFrame()
{
#ifdef HAVE_KLAYOUT
    if (!ensureViewWidget()) {
        return nullptr;
    }
    return m_viewWidget ? m_viewWidget->layer_control_frame() : nullptr;
#else
    return nullptr;
#endif
}

QWidget* KLayout2DView::hierarchyControlFrame()
{
#ifdef HAVE_KLAYOUT
    if (!ensureViewWidget()) {
        return nullptr;
    }
    return m_viewWidget ? m_viewWidget->hierarchy_control_frame() : nullptr;
#else
    return nullptr;
#endif
}

QStringList KLayout2DView::cellNames() const
{
    QStringList names;
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget || !m_viewWidget->view()) {
        return names;
    }
    try {
        lay::LayoutView* view = m_viewWidget->view();
        if (view->cellviews() == 0) {
            return names;
        }
        const lay::CellView& cv = view->cellview(0);
        if (!cv.is_valid()) {
            return names;
        }
        const db::Layout& layout = cv->layout();
        for (db::Layout::const_iterator ci = layout.begin(); ci != layout.end(); ++ci) {
            names.append(QString::fromStdString(std::string(layout.cell_name(ci->cell_index()))));
        }
    } catch (const tl::Exception& e) {
        qWarning("KLayout2DView::cellNames failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        qWarning("KLayout2DView::cellNames failed: %s", e.what());
    }
#endif
    return names;
}

QString KLayout2DView::currentCellName() const
{
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget || !m_viewWidget->view()) {
        return {};
    }
    try {
        lay::LayoutView* view = m_viewWidget->view();
        if (view->cellviews() == 0) {
            return {};
        }
        const lay::CellView& cv = view->cellview(0);
        if (!cv.is_valid()) {
            return {};
        }
        const db::Layout& layout = cv->layout();
        return QString::fromStdString(std::string(layout.cell_name(cv.cell_index())));
    } catch (const tl::Exception& e) {
        qWarning("KLayout2DView::currentCellName failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        qWarning("KLayout2DView::currentCellName failed: %s", e.what());
    }
#endif
    return {};
}

bool KLayout2DView::setCurrentCell(const QString& name)
{
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget || !m_viewWidget->view()) {
        return false;
    }
    try {
        lay::LayoutView* view = m_viewWidget->view();
        if (view->cellviews() == 0) {
            return false;
        }
        m_blockCellNavigation = true;
        lay::CellViewRef cvRef = view->cellview_ref(0);
        cvRef.set_cell(name.toStdString());
        if (cvRef.is_valid() && cvRef.cell()) {
            view->set_hier_levels(std::make_pair(0, int(cvRef.cell()->hierarchy_levels()) + 1));
        }
        view->zoom_fit();
        m_blockCellNavigation = false;
        emit cellChanged(name);
        return true;
    } catch (const tl::Exception& e) {
        m_blockCellNavigation = false;
        qWarning("KLayout2DView::setCurrentCell failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        m_blockCellNavigation = false;
        qWarning("KLayout2DView::setCurrentCell failed: %s", e.what());
    }
#else
    Q_UNUSED(name);
#endif
    return false;
}

int KLayout2DView::layerCount() const
{
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget || !m_viewWidget->view()) {
        return 0;
    }
    try {
        lay::LayoutView* view = m_viewWidget->view();
        int count = 0;
        for (auto it = view->begin_layers(); !it.at_end(); ++it) {
            ++count;
        }
        return count;
    } catch (const tl::Exception& e) {
        qWarning("KLayout2DView::layerCount failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        qWarning("KLayout2DView::layerCount failed: %s", e.what());
    }
#endif
    return 0;
}

void KLayout2DView::setAllLayersVisible(bool visible)
{
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget || !m_viewWidget->view()) {
        return;
    }
    try {
        // Layers only. A heading left hidden would hide every row under it
        // whatever that row's own box says.
        lay::LayoutView* view = m_viewWidget->view();
        for (auto it = view->begin_layers(); !it.at_end(); ++it) {
            if (it->has_children()) continue;
            lay::LayerPropertiesNode props(*it);
            props.set_visible(visible);
            view->replace_layer_node(it, props);
        }
    } catch (const tl::Exception& e) {
        qWarning("KLayout2DView::setAllLayersVisible failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        qWarning("KLayout2DView::setAllLayersVisible failed: %s", e.what());
    }
#else
    Q_UNUSED(visible);
#endif
}

void KLayout2DView::setLayerVisible(int index, bool visible)
{
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget || !m_viewWidget->view()) {
        return;
    }
    try {
        lay::LayoutView* view = m_viewWidget->view();
        int i = 0;
        for (auto it = view->begin_layers(); !it.at_end(); ++it) {
            if (i == index) {
                lay::LayerPropertiesNode props(*it);
                props.set_visible(visible);
                view->replace_layer_node(it, props);
                return;
            }
            ++i;
        }
    } catch (const tl::Exception& e) {
        qWarning("KLayout2DView::setLayerVisible failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        qWarning("KLayout2DView::setLayerVisible failed: %s", e.what());
    }
#else
    Q_UNUSED(index);
    Q_UNUSED(visible);
#endif
}

QVector<LayerInfo> KLayout2DView::layerInfos() const
{
    QVector<LayerInfo> result;
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget || !m_viewWidget->view()) {
        return result;
    }
    try {
        lay::LayoutView* view = m_viewWidget->view();
        int index = 0;
        for (auto it = view->begin_layers(); !it.at_end(); ++it) {
            LayerInfo info;
            info.index = index;
            info.visible = it->visible(true);
            info.isGroup = it->has_children();
            if (!it.at_top()) {
                const auto parent = it.parent();
                if (!parent.is_null()) {
                    info.group = QString::fromStdString(parent->name());
                }
            }

            // Get display name
            std::string name = it->name();
            if (!name.empty()) {
                info.name = QString::fromStdString(name);
            }

            // Get layer/datatype from source
            std::string srcStr = it->source_string(true);
            if (!srcStr.empty()) {
                if (info.name.isEmpty()) {
                    info.name = QString::fromStdString(srcStr);
                }
                // Parse layer/datatype from source string (format: "layer/datatype")
                const lay::ParsedLayerSource& src = it->source(true);
                info.layer = src.layer();
                info.datatype = src.datatype();
                if (!info.isGroup) {
                    auto meta = m_rowMeta.find({cellviewOf(src),
                                                LayerNum(info.layer, info.datatype)});
                    info.fill = meta != m_rowMeta.end() && meta->second.fill;
                }
            }

            if (info.name.isEmpty()) {
                info.name = QString("Layer %1").arg(index);
            }

            // Get fill color
            if (it->has_fill_color(true)) {
                tl::color_t fc = it->eff_fill_color(true);
                info.fillColor = QColor((fc >> 16) & 0xFF, (fc >> 8) & 0xFF, fc & 0xFF);
            }

            // Get frame color
            if (it->has_frame_color(true)) {
                tl::color_t ec = it->eff_frame_color(true);
                info.frameColor = QColor((ec >> 16) & 0xFF, (ec >> 8) & 0xFF, ec & 0xFF);
            }

            result.append(info);
            ++index;
        }
    } catch (const tl::Exception& e) {
        qWarning("KLayout2DView::layerInfos failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        qWarning("KLayout2DView::layerInfos failed: %s", e.what());
    }
#endif
    return result;
}

ViewBox KLayout2DView::fullBox() const
{
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget || !m_viewWidget->view()) {
        return {};
    }
    try {
        lay::LayoutView* view = m_viewWidget->view();
        if (view->cellviews() == 0) {
            return {};
        }
        db::DBox b = view->full_box();
        if (b.empty()) {
            return {};
        }
        return ViewBox{ b.left(), b.bottom(), b.right(), b.top() };
    } catch (const tl::Exception& e) {
        qWarning("KLayout2DView::fullBox failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        qWarning("KLayout2DView::fullBox failed: %s", e.what());
    }
#endif
    return {};
}

ViewBox KLayout2DView::visibleBox() const
{
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget || !m_viewWidget->view()) {
        return {};
    }
    try {
        lay::LayoutView* view = m_viewWidget->view();
        if (view->cellviews() == 0) {
            return {};
        }
        db::DBox b = view->box();
        if (b.empty()) {
            return {};
        }
        return ViewBox{ b.left(), b.bottom(), b.right(), b.top() };
    } catch (const tl::Exception& e) {
        qWarning("KLayout2DView::visibleBox failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        qWarning("KLayout2DView::visibleBox failed: %s", e.what());
    }
#endif
    return {};
}

QImage KLayout2DView::renderOverview(int maxWidth, int maxHeight) const
{
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget || !m_viewWidget->view()) {
        return {};
    }
    if (maxWidth <= 0 || maxHeight <= 0) {
        return {};
    }
    try {
        lay::LayoutView* view = m_viewWidget->view();
        if (view->cellviews() == 0) {
            return {};
        }
        db::DBox fb = view->full_box();
        if (fb.empty()) {
            return {};
        }

        // Size the image to the layout aspect ratio so the thumbnail maps 1:1
        // onto full_box (KLayout would otherwise letterbox internally, and the
        // overview's viewport rectangle would no longer align). The overview
        // widget does the letterboxing itself via OverviewTransform.
        const double bw = fb.width();
        const double bh = fb.height();
        int w = maxWidth;
        int h = maxHeight;
        if (bw * maxHeight > bh * maxWidth) {
            // box wider than the area: width-bound, shrink height
            h = std::max(1, static_cast<int>(std::lround(maxWidth * bh / bw)));
        } else {
            // box taller than the area: height-bound, shrink width
            w = std::max(1, static_cast<int>(std::lround(maxHeight * bw / bh)));
        }

        // 0 / invalid arguments make KLayout reuse the view's own oversampling,
        // resolution and colors; oversampling 2 gives a crisp anti-aliased
        // thumbnail. target_box = full_box() renders the WHOLE layout.
        return view->get_image_with_options(
            static_cast<unsigned int>(w), static_cast<unsigned int>(h),
            0,            // linewidth (auto)
            2,            // oversampling
            0.0, 0.0,     // resolution, font_resolution (auto)
            tl::Color(), tl::Color(), tl::Color(),  // background/foreground/active = view's
            fb,           // target box = full layout extent
            false);       // color (not monochrome)
    } catch (const tl::Exception& e) {
        qWarning("KLayout2DView::renderOverview failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        qWarning("KLayout2DView::renderOverview failed: %s", e.what());
    }
#else
    Q_UNUSED(maxWidth);
    Q_UNUSED(maxHeight);
#endif
    return {};
}

void KLayout2DView::zoomToBox(const ViewBox& box)
{
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget || !m_viewWidget->view() || !box.valid()) {
        return;
    }
    try {
        m_viewWidget->view()->zoom_box(
            db::DBox(box.xmin, box.ymin, box.xmax, box.ymax));
    } catch (const tl::Exception& e) {
        qWarning("KLayout2DView::zoomToBox failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        qWarning("KLayout2DView::zoomToBox failed: %s", e.what());
    }
#else
    Q_UNUSED(box);
#endif
}

void KLayout2DView::centerOn(double xUm, double yUm)
{
#ifdef HAVE_KLAYOUT
    if (!m_viewWidget || !m_viewWidget->view()) {
        return;
    }
    try {
        m_viewWidget->view()->pan_center(db::DPoint(xUm, yUm));
    } catch (const tl::Exception& e) {
        qWarning("KLayout2DView::centerOn failed: %s", e.msg().c_str());
    } catch (const std::exception& e) {
        qWarning("KLayout2DView::centerOn failed: %s", e.what());
    }
#else
    Q_UNUSED(xUm);
    Q_UNUSED(yUm);
#endif
}

} // namespace chiplet
