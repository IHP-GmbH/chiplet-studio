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
#include <QGuiApplication>
#include <QScreen>

#ifdef HAVE_KLAYOUT
#include "layLayoutView.h"
#include "layLayerProperties.h"
#include "layParsedLayerSource.h"
#include "layCellView.h"
#include "dbLayout.h"
#include "dbLoadLayoutOptions.h"
#include "tlException.h"
#include "tlObject.h"
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
#endif

KLayout2DView::KLayout2DView(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

KLayout2DView::~KLayout2DView()
{
#ifdef HAVE_KLAYOUT
    // Destroy bridge before view widget to avoid dangling event subscriptions
    m_cellViewBridge.reset();
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

        // Load layer properties if provided
        if (!lypPath.isEmpty()) {
            view->load_layer_props(lypPath.toStdString());
        } else {
            // Black-box / no-LYP chiplet (commercial / closed PDK node): KLayout
            // auto-creates a visible node for every detected GDS layer, so the
            // metal pads render with default colors. Guarantee the pad-name text
            // shows -- KLayout draws GDS TEXT records natively -- with a
            // high-contrast color and no lazy drop on small pads.
            view->text_visible(true);
            view->text_lazy_rendering(false);
            view->text_color(tl::Color(255, 255, 255));
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
        lay::LayoutView* view = m_viewWidget->view();
        for (auto it = view->begin_layers(); !it.at_end(); ++it) {
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

} // namespace chiplet
