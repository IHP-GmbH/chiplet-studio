// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * ViewingNotesBar.h - One line above the views saying what they could not show
 *
 * "3 viewing notes" with an information icon; clicking it lists them. Hidden
 * while there is nothing to say. Text and icon only, no colour coding.
 */

#ifndef CHIPLET_UI_VIEWINGNOTESBAR_H
#define CHIPLET_UI_VIEWINGNOTESBAR_H

#include <QStringList>
#include <QWidget>

class QPlainTextEdit;
class QToolButton;

namespace chiplet {

class ViewingNotesBar : public QWidget {
    Q_OBJECT

public:
    explicit ViewingNotesBar(QWidget* parent = nullptr);

    // Replace the notes. Duplicates are dropped, order kept. An empty list
    // hides the bar and collapses it.
    void setNotes(const QStringList& notes);
    const QStringList& notes() const { return m_notes; }

    QString summary() const;  // the one-line text, e.g. "3 viewing notes"
    bool isExpanded() const;
    void setExpanded(bool expanded);

private:
    QStringList m_notes;
    QToolButton* m_toggle = nullptr;
    QPlainTextEdit* m_list = nullptr;
};

} // namespace chiplet

#endif // CHIPLET_UI_VIEWINGNOTESBAR_H
