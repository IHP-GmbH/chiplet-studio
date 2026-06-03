// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * EmbeddedDispatcher.h - Minimal KLayout dispatcher for embedded LayoutViewWidget usage
 *
 * Properly initializes menu infrastructure to prevent plugin crashes when
 * using KLayout's LayoutViewWidget in an embedded context.
 */

#ifndef CHIPLET_VIEW2D_EMBEDDEDDISPATCHER_H
#define CHIPLET_VIEW2D_EMBEDDEDDISPATCHER_H

#ifdef HAVE_KLAYOUT

#include "layDispatcher.h"
#include <QWidget>

namespace chiplet {

/**
 * @brief Minimal KLayout dispatcher for embedded LayoutViewWidget usage
 *
 * KLayout's LayoutViewWidget constructor triggers plugin initialization,
 * which requires dispatcher->menu() to be available. This class ensures
 * the menu infrastructure is properly created before any plugin init.
 */
class EmbeddedDispatcher : public lay::Dispatcher
{
public:
    /**
     * @brief Construct embedded dispatcher
     * @param menuParent Optional parent widget for menu infrastructure
     */
    explicit EmbeddedDispatcher(QWidget* menuParent = nullptr);

    ~EmbeddedDispatcher() override;

    // Prevent copying
    EmbeddedDispatcher(const EmbeddedDispatcher&) = delete;
    EmbeddedDispatcher& operator=(const EmbeddedDispatcher&) = delete;

    /**
     * @brief Check if properly initialized
     * @return true if menu infrastructure is ready
     */
    bool isInitialized() const { return m_initialized; }

private:
    bool m_initialized = false;
};

} // namespace chiplet

#endif // HAVE_KLAYOUT
#endif // CHIPLET_VIEW2D_EMBEDDEDDISPATCHER_H
