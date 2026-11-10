// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * PanelWiring.h - connections between the panels that MainWindow makes
 *
 * MainWindow is not in the test target, so a connection made inline there can
 * go missing without a test noticing. The Hierarchy's Properties action is the
 * case that did: the signal was emitted and nothing received it. The
 * connections live here instead, where a test can make them on real panels
 * and drive them.
 */

#ifndef CHIPLET_UI_PANELWIRING_H
#define CHIPLET_UI_PANELWIRING_H

#include "HierarchyPanel.h"
#include "PropertiesPanel.h"
#include "core/Assembly.h"
#include "core/CommandProcessor.h"
#include "core/Component.h"
#include "view3d/AssemblyView.h"

#include <QDockWidget>
#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <memory>

namespace chiplet {

/**
 * Hierarchy right-click > Properties shows that component in the Properties
 * panel and brings its dock to the front. The dock shares a tab area with the
 * 2D Layout, so showing the panel without raising the dock changes a tab the
 * user cannot see.
 *
 * @param assembly  returns the open assembly when the request arrives; it is
 *                  replaced on every open, so it is not captured as a pointer
 */
inline void connectPropertiesRequest(HierarchyPanel* hierarchy, PropertiesPanel* properties,
                                     QDockWidget* dock, std::function<Assembly*()> assembly)
{
    QObject::connect(hierarchy, &HierarchyPanel::showPropertiesRequested, properties,
        [properties, dock, assembly = std::move(assembly)](const QString& componentId) {
            properties->setComponent(componentId.toStdString(), assembly());
            dock->show();
            dock->raise();
        });
}

/**
 * A change of a component's render mode reaches the 3D view and the
 * Hierarchy's Mode column however it was made: from the Hierarchy menu, by
 * undo or redo, or by a script's command. Only the menu used to pass it on,
 * so undo changed the model and left both views showing the old mode.
 *
 * The processor says only that the model changed, so each component's mode
 * is compared with the one last passed on.
 */
inline void connectRenderModeSync(CommandProcessor* processor, AssemblyView* view,
                                  HierarchyPanel* hierarchy)
{
    auto modes = [processor]() {
        std::map<ComponentID, RenderMode> now;
        if (const Assembly* assembly = processor->assembly()) {
            for (const auto& comp : assembly->components()) {
                now[comp->id()] = comp->render_mode();
            }
        }
        return now;
    };
    auto passedOn = std::make_shared<std::map<ComponentID, RenderMode>>(modes());
    QObject::connect(processor, &CommandProcessor::model_changed, view,
        [view, hierarchy, modes, passedOn]() {
            for (const auto& [id, mode] : modes()) {
                auto last = passedOn->find(id);
                if (last != passedOn->end() && last->second == mode) continue;
                (*passedOn)[id] = mode;
                const QString componentId = QString::fromStdString(id);
                view->onComponentRenderModeChanged(componentId, mode);
                hierarchy->updateRenderModeDisplay(componentId);
            }
        });
}

} // namespace chiplet

#endif // CHIPLET_UI_PANELWIRING_H
