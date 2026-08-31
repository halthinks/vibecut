/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"

#include "vibecut/vibecutprojectrevision.h"

#include <QUndoCommand>
#include <QUndoStack>

TEST_CASE("project revision stays unique across undo branches", "[vibecut][revision]")
{
    QUndoStack stack;
    VibeCutProjectRevisionTracker tracker;
    tracker.observe(&stack);

    const quint64 initial = tracker.revision();
    stack.push(new QUndoCommand(QStringLiteral("first edit")));
    const quint64 firstEdit = tracker.revision();
    CHECK(firstEdit > initial);
    CHECK(stack.index() == 1);

    stack.undo();
    const quint64 afterUndo = tracker.revision();
    CHECK(afterUndo > firstEdit);
    CHECK(stack.index() == 0);

    stack.push(new QUndoCommand(QStringLiteral("different edit")));
    const quint64 branchedEdit = tracker.revision();
    CHECK(branchedEdit > afterUndo);
    CHECK(stack.index() == 1);

    // The numeric undo index collided with the old state, but the VibeCut
    // revision token did not. A plan made at firstEdit is therefore stale.
    CHECK(branchedEdit != firstEdit);
}

TEST_CASE("switching active project stacks invalidates an old plan", "[vibecut][revision]")
{
    QUndoStack first;
    QUndoStack second;
    VibeCutProjectRevisionTracker tracker;

    tracker.observe(&first);
    const quint64 firstRevision = tracker.revision();
    tracker.observe(&second);
    CHECK(tracker.revision() > firstRevision);
}
