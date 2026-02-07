/*
 * EmbeddedDispatcher.cpp - Implementation of minimal KLayout dispatcher
 */

#include "EmbeddedDispatcher.h"

#ifdef HAVE_KLAYOUT

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

    m_initialized = true;
}

EmbeddedDispatcher::~EmbeddedDispatcher() = default;

} // namespace chiplet

#endif // HAVE_KLAYOUT
