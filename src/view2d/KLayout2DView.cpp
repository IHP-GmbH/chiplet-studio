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
