// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * ViewingNotesBar.h - One line above the views saying what they could not show
 *
 * "3 viewing notes" with an information icon; clicking it lists them. A note
 * that must be read (a guessed mount plane) opens the list when it arrives.
 * Hidden while there is nothing to say. Text and icon only, no colour coding.
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
    // hides the bar and collapses it. `opening` names, by a key the caller
    // keeps stable (not the note's text, which can name the file), the
    // reasons to open the list so a note is read rather than counted. Each
    // key opens it once per document: a list the user closes stays closed.
    void setNotes(const QStringList& notes, const QStringList& opening = {});
    // A new document: every opening key may open the list again.
    void startDocument() { m_openedFor.clear(); }
    const QStringList& notes() const { return m_notes; }

    QString summary() const;  // the one-line text, e.g. "3 viewing notes"
    bool isExpanded() const;
    void setExpanded(bool expanded);

private:
    QStringList m_notes;
    QStringList m_openedFor;  // opening keys that already opened the list
    QToolButton* m_toggle = nullptr;
    QPlainTextEdit* m_list = nullptr;
};

} // namespace chiplet

#endif // CHIPLET_UI_VIEWINGNOTESBAR_H
