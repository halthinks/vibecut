/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecutprojectrevision.h"

#include <QUndoStack>

VibeCutProjectRevisionTracker::VibeCutProjectRevisionTracker(QObject *parent)
    : QObject(parent)
{
}

void VibeCutProjectRevisionTracker::observe(QUndoStack *stack)
{
    if (m_stack == stack) {
        return;
    }

    QObject::disconnect(m_indexConnection);
    m_stack = stack;
    ++m_revision;

    if (m_stack) {
        m_indexConnection = connect(m_stack, &QUndoStack::indexChanged, this, [this](int) { ++m_revision; });
    }
}
