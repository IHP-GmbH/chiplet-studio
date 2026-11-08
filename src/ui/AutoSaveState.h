// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * AutoSaveState.h - what the user is told when an auto-save fails.
 *
 * This is a separate, display-free type for one reason: the rule it encodes is
 * the whole fix, and inside MainWindow it could only be asserted by a test that
 * needs a running GUI. A behaviour with no witness is a claim, not a guarantee.
 */

#ifndef CHIPLET_UI_AUTOSAVESTATE_H
#define CHIPLET_UI_AUTOSAVESTATE_H

namespace chiplet {

// What a caller must show after one auto-save attempt.
struct AutoSaveNotice {
    // Interrupt the user. True only for the FIRST failure of a run.
    bool announce = false;
    // Keep a visible marker up. True for every failure, false once one succeeds.
    bool warn = false;
};

/**
 * The notice policy for a periodic auto-save.
 *
 * Auto-save runs every five minutes, unattended, and the failure it has to
 * report is not "an operation you just asked for did not work". It is "the
 * thing you believe is protecting your work is not". Those need different
 * notices, and the previous one got both halves wrong: a five second status-bar
 * message, which is over before a user looking at the 3D view can notice it,
 * and which then leaves nothing behind while the condition it reported is still
 * true. The user goes on believing the file is being kept.
 *
 * So the rule is that the notice's LIFETIME matches the condition's. A marker
 * stays up for as long as auto-save is failing and disappears when one
 * succeeds, which is the part a transient message cannot do.
 *
 * The interruption is separate and fires once per run of failures, not once per
 * failure. Every five minutes forever is not a stronger warning than one, it is
 * a thing people learn to dismiss without reading, and it would punish exactly
 * the user who left the tool open. A run that recovers and fails again does
 * announce again: that is a new event, and the recovery in between is what
 * makes it one.
 */
class AutoSaveState {
public:
    AutoSaveNotice on_failure()
    {
        const bool first = !m_failing;
        m_failing = true;
        return AutoSaveNotice{first, true};
    }

    AutoSaveNotice on_success()
    {
        m_failing = false;
        return AutoSaveNotice{false, false};
    }

    // True while the last attempt failed. Exposed for the tests and for a
    // caller that wants to render the current state without an attempt.
    bool failing() const { return m_failing; }

private:
    bool m_failing = false;
};

} // namespace chiplet

#endif // CHIPLET_UI_AUTOSAVESTATE_H
