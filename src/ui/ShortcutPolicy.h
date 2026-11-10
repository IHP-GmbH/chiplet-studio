// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ShortcutPolicy.h - Fallback for standard keys a platform theme leaves unbound
 *
 * QKeySequence::StandardKey resolves through the platform theme, and a theme is
 * free to supply nothing for a given key. When it does, the action silently ends
 * up with no shortcut at all: the menu shows a blank accelerator and the key is
 * never dispatched. Observed with QKeySequence::SaveAs under the generic theme,
 * while New, Open, Save, Refresh and Quit all resolved in the same window.
 *
 * The policy here is deliberately conservative: the platform bindings win
 * whenever there are any, and the literal fallback applies only to the empty
 * case. That holds whichever way the emptiness arises, so it does not depend on
 * knowing why a particular theme has no entry.
 */

#ifndef CHIPLET_UI_SHORTCUTPOLICY_H
#define CHIPLET_UI_SHORTCUTPOLICY_H

#include <QKeySequence>
#include <QList>
#include <QString>

namespace chiplet {

/**
 * Returns @p standard unchanged when the platform supplied any binding.
 * Only when it is empty does @p fallback apply.
 *
 * The emptiness check on the fallback covers an empty string and nothing more.
 * It is not a validator: QKeySequence("~~~") parses to a NON-empty sequence
 * whose toString() is empty, measured on Qt 6.4.2, so a malformed literal would
 * install an unusable binding. The one caller passes a fixed valid literal, so
 * that path does not arise; do not read this as input validation.
 */
inline QList<QKeySequence> shortcutsOrFallback(const QList<QKeySequence>& standard,
                                               const QString& fallback)
{
    if (!standard.isEmpty()) {
        return standard;
    }
    const QKeySequence seq(fallback);
    if (seq.isEmpty()) {
        return {};
    }
    return {seq};
}

} // namespace chiplet

#endif // CHIPLET_UI_SHORTCUTPOLICY_H
