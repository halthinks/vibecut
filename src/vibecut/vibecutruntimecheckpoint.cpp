/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutruntimecheckpoint.h"

#include "core.h"
#include "doc/kdenlivedoc.h"
#include "doc/docundostack.hpp"

#include <memory>
#include <utility>

namespace {
std::shared_ptr<DocUndoStack> currentUndoStack()
{
    if (!pCore || !pCore->currentDoc()) return std::shared_ptr<DocUndoStack>();
    return pCore->currentDoc()->commandStack();
}

QString checkpointLabel(const QString &objective)
{
    const QString trimmed = objective.trimmed();
    return QStringLiteral("VibeCut Runtime: %1").arg(trimmed.isEmpty() ? QStringLiteral("approved plan") : trimmed.left(80));
}
} // namespace

VibeCutRuntimeCheckpoint::VibeCutRuntimeCheckpoint() = default;

VibeCutRuntimeCheckpoint::VibeCutRuntimeCheckpoint(BeginFn begin, EndFn end, RollbackFn rollback)
    : m_begin(std::move(begin))
    , m_end(std::move(end))
    , m_rollback(std::move(rollback))
    , m_injectedCallbacks(true)
{
}

bool VibeCutRuntimeCheckpoint::beginForMutation(const QString &objective, QString *error)
{
    if (error) error->clear();
    if (m_open) return true;

    if (m_injectedCallbacks) {
        if (!m_begin || !m_begin(checkpointLabel(objective))) {
            if (error) *error = QStringLiteral("Could not begin the adapter-side Kdenlive Undo checkpoint.");
            return false;
        }
        m_startIndex = -1;
        m_open = true;
        return true;
    }

    const std::shared_ptr<DocUndoStack> stack = currentUndoStack();
    if (!stack) {
        if (error) *error = QStringLiteral("Could not begin the adapter-side Kdenlive Undo checkpoint: no current undo stack.");
        return false;
    }
    m_startIndex = stack->index();
    stack->beginMacro(checkpointLabel(objective));
    m_open = true;
    return true;
}

bool VibeCutRuntimeCheckpoint::commitBeforeAsync(QString *error)
{
    return commitForCompletion(error);
}

bool VibeCutRuntimeCheckpoint::commitForCompletion(QString *error)
{
    if (error) error->clear();
    if (!m_open) return true;

    if (m_injectedCallbacks) {
        if (!m_end || !m_end()) {
            if (error) *error = QStringLiteral("Could not close the adapter-side Kdenlive Undo checkpoint.");
            return false;
        }
    } else {
        const std::shared_ptr<DocUndoStack> stack = currentUndoStack();
        if (!stack) {
            if (error) *error = QStringLiteral("Could not close the adapter-side Kdenlive Undo checkpoint: no current undo stack.");
            return false;
        }
        stack->endMacro();
    }

    m_open = false;
    m_startIndex = -1;
    ++m_committedCount;
    return true;
}

bool VibeCutRuntimeCheckpoint::rollbackOpen(QString *error)
{
    if (error) error->clear();
    if (!m_open) return true;

    if (m_injectedCallbacks) {
        if (!m_rollback || !m_rollback()) {
            if (error) *error = QStringLiteral("Could not roll back the current adapter-side Kdenlive Undo checkpoint.");
            return false;
        }
    } else {
        const std::shared_ptr<DocUndoStack> stack = currentUndoStack();
        if (!stack || m_startIndex < 0) {
            if (error) *error = QStringLiteral("Could not roll back the current adapter-side Kdenlive Undo checkpoint: checkpoint origin is unavailable.");
            return false;
        }
        const int targetIndex = m_startIndex;
        stack->endMacro();
        if (stack->index() < targetIndex) {
            if (error) *error = QStringLiteral("Undo stack regressed below the checkpoint origin; refusing further rollback.");
            return false;
        }
        while (stack->index() > targetIndex && stack->canUndo()) stack->undo();
        if (stack->index() != targetIndex) {
            if (error) *error = QStringLiteral("Could not restore the exact undo-stack index captured before the runtime checkpoint.");
            return false;
        }
    }

    m_open = false;
    m_startIndex = -1;
    ++m_rolledBackCount;
    return true;
}

void VibeCutRuntimeCheckpoint::reset()
{
    m_open = false;
    m_startIndex = -1;
    m_committedCount = 0;
    m_rolledBackCount = 0;
}
