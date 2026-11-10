// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ViewingNotesBar.h"

#include <QPlainTextEdit>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace chiplet {

ViewingNotesBar::ViewingNotesBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("viewingNotesBar");

    m_toggle = new QToolButton(this);
    m_toggle->setObjectName("viewingNotesToggle");
    m_toggle->setCheckable(true);
    m_toggle->setAutoRaise(true);
    m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toggle->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    m_toggle->setToolTip(tr("What the views could not show as the file asks. Click to list."));

    m_list = new QPlainTextEdit(this);
    m_list->setObjectName("viewingNotesList");
    m_list->setReadOnly(true);
    m_list->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_list->setMaximumHeight(fontMetrics().lineSpacing() * 8);
    m_list->setVisible(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);
    layout->addWidget(m_toggle, 0, Qt::AlignLeft);
    layout->addWidget(m_list);

    connect(m_toggle, &QToolButton::toggled, this, [this](bool on) { m_list->setVisible(on); });

    setVisible(false);
}

void ViewingNotesBar::setNotes(const QStringList& notes, const QStringList& opening)
{
    bool open = false;
    for (const QString& key : opening) {
        if (!key.isEmpty() && !m_openedFor.contains(key)) {
            m_openedFor << key;
            open = true;
        }
    }
    m_notes.clear();
    for (const QString& n : notes) {
        if (!n.isEmpty() && !m_notes.contains(n)) {
            m_notes << n;
        }
    }
    m_toggle->setText(summary());
    QStringList lines;
    for (const QString& n : m_notes) {
        lines << QStringLiteral("- ") + n;
    }
    m_list->setPlainText(lines.join('\n'));
    if (m_notes.isEmpty()) {
        setExpanded(false);
    } else if (open) {
        setExpanded(true);
    }
    setVisible(!m_notes.isEmpty());
}

QString ViewingNotesBar::summary() const
{
    return m_notes.size() == 1 ? tr("1 viewing note")
                               : tr("%1 viewing notes").arg(m_notes.size());
}

bool ViewingNotesBar::isExpanded() const
{
    return m_toggle->isChecked();
}

void ViewingNotesBar::setExpanded(bool expanded)
{
    m_toggle->setChecked(expanded);
}

} // namespace chiplet
