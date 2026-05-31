/*
 * EmbeddedDispatcher.cpp - Implementation of minimal KLayout dispatcher
 */

#include "EmbeddedDispatcher.h"

#ifdef HAVE_KLAYOUT

#include "layAbstractMenu.h"
#include "layPlugin.h"
#include "tlClassRegistry.h"
#include "tlException.h"

#include <QMenuBar>
#include <QToolBar>
#include <QtGlobal>

namespace chiplet {

EmbeddedDispatcher::EmbeddedDispatcher(QWidget* menuParent)
    : lay::Dispatcher(nullptr, true)  // standalone=true, no parent
{
    // Set menu parent widget if provided
    if (menuParent) {
        set_menu_parent_widget(menuParent);
    }

    // CRITICAL: Create menu infrastructure BEFORE any plugin initialization.
    // Plugins call dispatcher->menu() during init, which crashes if null.
    make_menu();

    // Replicate the relevant part of lay::LayoutViewBase::init_menu(): KLayout
    // only auto-initializes the menus when the LayoutView is its OWN (root)
    // dispatcher - finish() calls init_menu() only when dispatcher() == view.
    // Because we attach an EXTERNAL dispatcher to the embedded LayoutViewWidget,
    // that path is skipped and the detached context menus (@hcp_context_menu,
    // @lcp_context_menu) are never populated nor built. detached_menu() then
    // returns a null QMenu and right-clicking the hierarchy/layer panel
    // dereferences null in QMenu::exec() -> SIGSEGV.
    //
    // We deliberately do NOT iterate ALL plugin declarations: many assume a fully
    // built main menu and dereference null when their entries copy from / insert
    // next to base menu paths (zoom_menu, @toolbar, ...) that do not exist in this
    // minimal dispatcher - that itself segfaults inside init_menu(). We only need
    // the two side-panel plugins, whose entries are self-contained (no copy_from,
    // no mouse-mode), so initializing just those is safe and brings in the real
    // context-menu actions ("Show As New Top", sorting, per-layer operations).
    static const char* const kPanelPlugins[] = {
        "HierarchyControlPanelPlugin",
        "LayerControlPanelPlugin",
    };
    try {
        for (tl::Registrar<lay::PluginDeclaration>::iterator cls = tl::Registrar<lay::PluginDeclaration>::begin ();
             cls != tl::Registrar<lay::PluginDeclaration>::end (); ++cls) {
            for (const char* name : kPanelPlugins) {
                if (cls.current_name() == name) {
                    const_cast<lay::PluginDeclaration *>(&*cls)->init_menu(this);
                    break;
                }
            }
        }
    } catch (const tl::Exception& ex) {
        qWarning("EmbeddedDispatcher: panel menu init failed: %s", ex.msg().c_str());
    } catch (const std::exception& ex) {
        qWarning("EmbeddedDispatcher: panel menu init failed: %s", ex.what());
    } catch (...) {
        qWarning("EmbeddedDispatcher: panel menu init failed (unknown)");
    }

    if (menu()) {
        // Safety net: ensure the detached context menus the side panels rely on
        // exist even if the corresponding plugins were not registered.
        if (!menu()->is_valid("@hcp_context_menu")) {
            menu()->insert_menu("end", "@hcp_context_menu", "");
        }
        if (!menu()->is_valid("@lcp_context_menu")) {
            menu()->insert_menu("end", "@lcp_context_menu", "");
        }

        // Materialize the physical QMenu objects. AbstractMenu only creates the
        // QMenu for "@"-prefixed detached items during build(); in a normal app
        // the MainWindow calls build() with its menu bar/toolbar. We have neither,
        // so build with null targets - build() still walks the abstract tree and
        // creates a (possibly empty but non-null) QMenu for every detached item.
        menu()->build(static_cast<QMenuBar*>(nullptr), static_cast<QToolBar*>(nullptr));
    }

    m_initialized = true;
}

EmbeddedDispatcher::~EmbeddedDispatcher() = default;

} // namespace chiplet

#endif // HAVE_KLAYOUT
