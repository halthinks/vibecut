/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QString>

#include <functional>

/** Adapter-side synchronous checkpoint controller for out-of-process plans.
 *
 * Semantics intentionally mirror VibeCutPlanRuntime:
 * - consecutive synchronous project mutations share one open Undo macro;
 * - the macro is committed before asynchronous work;
 * - a failed synchronous mutation rolls back only the currently open macro;
 * - completion commits any open macro;
 * - abort rolls back an open macro but never claims already-committed macros
 *   were undone.
 *
 * The production constructor binds to Kdenlive's current DocUndoStack. The
 * callback constructor exists so these state transitions can be unit-tested
 * without an editor.
 */
class VibeCutRuntimeCheckpoint
{
public:
    using BeginFn = std::function<bool(const QString &label)>;
    using EndFn = std::function<bool()>;
    using RollbackFn = std::function<bool()>;

    VibeCutRuntimeCheckpoint();
    VibeCutRuntimeCheckpoint(BeginFn begin, EndFn end, RollbackFn rollback);

    bool beginForMutation(const QString &objective, QString *error = nullptr);
    bool commitBeforeAsync(QString *error = nullptr);
    bool commitForCompletion(QString *error = nullptr);
    bool rollbackOpen(QString *error = nullptr);

    bool macroOpen() const { return m_open; }
    int committedCheckpointCount() const { return m_committedCount; }
    int rolledBackCheckpointCount() const { return m_rolledBackCount; }

    void reset();

private:
    BeginFn m_begin;
    EndFn m_end;
    RollbackFn m_rollback;
    bool m_open = false;
    int m_committedCount = 0;
    int m_rolledBackCount = 0;
};
