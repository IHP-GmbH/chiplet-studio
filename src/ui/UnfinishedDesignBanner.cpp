// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/UnfinishedDesignBanner.h"

#include "core/DocumentPersistence.h"

namespace chiplet {

UnfinishedDesignBanner::UnfinishedDesignBanner(QWidget* parent)
    : QLabel(parent)
{
    setWordWrap(true);
    setTextFormat(Qt::PlainText);
    setTextInteractionFlags(Qt::TextSelectableByMouse);  // the command can be copied
    setFrameShape(QFrame::StyledPanel);
    setContentsMargins(8, 4, 8, 4);
    hide();
}

QString UnfinishedDesignBanner::textFor(const QString& finalizer)
{
    return tr("This design is not finished: the flow wrote it before its last step, so "
              "positions and connections may still change. It is opened read-only; editing "
              "and saving are off. To get the final design, run %1 and open its result.")
        .arg(finalizer);
}

void UnfinishedDesignBanner::showFor(const DocumentPersistence& document)
{
    if (!document.read_only()) {
        clear();
        hide();
        return;
    }
    // The command comes from the file: one line, and not so long that the
    // banner pushes the views away.
    QString finalizer = QString::fromStdString(document.finalizer()).simplified();
    constexpr int kMaxFinalizer = 200;
    if (finalizer.size() > kMaxFinalizer) finalizer = finalizer.left(kMaxFinalizer) + "...";
    setText(textFor(finalizer));
    show();
}

} // namespace chiplet
