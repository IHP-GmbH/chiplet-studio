// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * UnfinishedDesignBanner.h - One line above the views for a design opened
 * before its last step
 *
 * A .chiplet the flow wrote before its last step (_metadata.finalize_required)
 * opens read-only. This says so, and names the command that finishes it.
 * Hidden for every other design.
 */
#ifndef CHIPLET_UI_UNFINISHEDDESIGNBANNER_H
#define CHIPLET_UI_UNFINISHEDDESIGNBANNER_H

#include <QLabel>

namespace chiplet {

struct DocumentPersistence;

class UnfinishedDesignBanner : public QLabel {
    Q_OBJECT
public:
    explicit UnfinishedDesignBanner(QWidget* parent = nullptr);

    // Show the banner for a read-only design, hide it for any other.
    void showFor(const DocumentPersistence& document);

    // The banner's words for a design that `finalizer` finishes.
    static QString textFor(const QString& finalizer);
};

} // namespace chiplet

#endif // CHIPLET_UI_UNFINISHEDDESIGNBANNER_H
