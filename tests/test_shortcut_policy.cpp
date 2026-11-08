/**
 * test_shortcut_policy.cpp - Fallback for standard keys a theme leaves unbound
 *
 * A rehearsal on the pinned image showed "Save As..." with a blank accelerator
 * while New, Open, Save, Reload and Exit all displayed theirs: the platform
 * theme resolved QKeySequence::SaveAs to nothing, so the action carried no key
 * and Ctrl+Shift+S was never dispatched. These tests pin the policy, not the
 * environment: the platform wins whenever it supplies anything, and the literal
 * only fills an empty result.
 */

#include <gtest/gtest.h>
#include <QKeySequence>
#include <QList>
#include "ui/ShortcutPolicy.h"

namespace chiplet {

TEST(ShortcutPolicy, EmptyPlatformBindingTakesTheFallback) {
    const QList<QKeySequence> out =
        shortcutsOrFallback({}, QStringLiteral("Ctrl+Shift+S"));
    ASSERT_EQ(out.size(), 1);
    EXPECT_EQ(out.first(), QKeySequence(QStringLiteral("Ctrl+Shift+S")));
}

TEST(ShortcutPolicy, PlatformBindingWinsAndTheFallbackIsIgnored) {
    const QList<QKeySequence> platform{QKeySequence(QStringLiteral("Ctrl+Alt+S"))};
    const QList<QKeySequence> out =
        shortcutsOrFallback(platform, QStringLiteral("Ctrl+Shift+S"));
    EXPECT_EQ(out, platform);
}

TEST(ShortcutPolicy, EveryPlatformBindingIsKept) {
    const QList<QKeySequence> platform{
        QKeySequence(QStringLiteral("Ctrl+Alt+S")),
        QKeySequence(QStringLiteral("F12")),
    };
    const QList<QKeySequence> out =
        shortcutsOrFallback(platform, QStringLiteral("Ctrl+Shift+S"));
    ASSERT_EQ(out.size(), 2);
    EXPECT_EQ(out, platform);
}

TEST(ShortcutPolicy, AnEmptyFallbackYieldsNothing) {
    // Emptiness only. A malformed string is a different question and not this
    // feature's: the one caller passes a fixed valid literal.
    const QList<QKeySequence> out = shortcutsOrFallback({}, QString());
    EXPECT_TRUE(out.isEmpty());
}

} // namespace chiplet
