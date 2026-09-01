/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QtGlobal>

class QUndoStack;

/** Monotonic live-project token used to reject stale agent plans.
 *
 * QUndoStack::index() alone is not a safe revision: undoing and then making a
 * different edit can return to the same numeric index with different project
 * state. This tracker increments on every observed index change and whenever
 * the active stack changes, so that branch-after-undo cannot collide with an
 * older plan token.
 */
class VibeCutProjectRevisionTracker : public QObject
{
    Q_OBJECT
public:
    explicit VibeCutProjectRevisionTracker(QObject *parent = nullptr);

    void observe(QUndoStack *stack);
    quint64 revision() const { return m_revision; }

private:
    QPointer<QUndoStack> m_stack;
    QMetaObject::Connection m_indexConnection;
    quint64 m_revision = 1;
};
