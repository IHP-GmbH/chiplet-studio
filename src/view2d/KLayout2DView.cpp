/*
 * KLayout2DView.cpp - Implementation of KLayout 2D view wrapper
 *
 * Uses lazy initialization to avoid crashes in headless environments.
 * The LayoutViewWidget is only created when actually needed and display is available.
 */

#include "KLayout2DView.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QGuiApplication>
#include <QScreen>

#ifdef HAVE_KLAYOUT
#include "layLayoutView.h"
#include "dbLoadLayoutOptions.h"
#include "tlException.h"
#endif

namespace chiplet {

KLayout2DView::KLayout2DView(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

KLayout2DView::~KLayout2DView() = default;

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
    // Already initialized successfully
    if (m_viewWidget && m_viewAvailable) {
        return true;
    }

    // Already tried and failed
    if (m_initAttempted && !m_viewAvailable) {
        return false;
    }

    m_initAttempted = true;

    // Check if display is available
    if (!isDisplayAvailable()) {
        qWarning("KLayout2DView: No display available, view disabled");
        return false;
    }

    try {
        // Create KLayout view widget
        // Options: LV_Normal = full features (layer panel, hierarchy panel, etc.)
        m_viewWidget = new lay::LayoutViewWidget(
            nullptr,    // No undo manager (read-only)
            false,      // Not editable
            nullptr,    // No plugin parent
            this,
            lay::LayoutView::LV_Normal
        );

        // Add to layout (find existing layout)
        QLayout* existingLayout = layout();
        if (existingLayout) {
            // Remove placeholder if present
            while (existingLayout->count() > 0) {
                QLayoutItem* item = existingLayout->takeAt(0);
                if (item->widget()) {
                    item->widget()->deleteLater();
                }
                delete item;
            }
            existingLayout->addWidget(m_viewWidget);
        }

        connectSignals();
        m_viewAvailable = true;
        return true;

    } catch (const std::exception& e) {
        qWarning("KLayout2DView: Failed to create view widget: %s", e.what());
        m_viewWidget = nullptr;
        m_viewAvailable = false;
        return false;
    } catch (...) {
        qWarning("KLayout2DView: Failed to create view widget (unknown error)");
        m_viewWidget = nullptr;
        m_viewAvailable = false;
        return false;
    }
#else
    return false;
#endif
}

void KLayout2DView::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Always start with a placeholder - real widget created lazily
    QLabel* placeholder = new QLabel(this);
#ifdef HAVE_KLAYOUT
    placeholder->setText("2D View (double-click component to load)");
#else
    placeholder->setText("2D View (KLayout not available)");
#endif
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet("background-color: #2a2a2a; color: #888;");
    mainLayout->addWidget(placeholder);
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
        }

        // Zoom to fit
        view->zoom_fit();

        m_currentPath = path;
        emit layoutChanged(true);
        return true;

    } catch (const tl::Exception& e) {
        qWarning("KLayout2DView::loadLayout failed: %s", e.msg().c_str());
        m_currentPath.clear();
        emit layoutChanged(false);
        return false;
    } catch (const std::exception& e) {
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

QWidget* KLayout2DView::layerControlFrame()
{
#ifdef HAVE_KLAYOUT
    // Ensure widget is created first
    if (!ensureViewWidget()) {
        return nullptr;
    }
    return m_viewWidget ? m_viewWidget->layer_control_frame() : nullptr;
#else
    return nullptr;
#endif
}

} // namespace chiplet
