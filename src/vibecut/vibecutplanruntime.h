/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once

#include "vibecutcontracts.h"
#include "vibecutprojectsnapshot.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class VibeCutToolSurface;

/** Stateful plan -> approve -> checkpointed execution runtime.
 *
 * Model output becomes a VibeCutEditPlan first. Execution is deterministic,
 * revision-guarded, policy-aware, groups contiguous synchronous edits into an
 * undo macro, and pauses on trackable async jobs until JobManager reports a
 * terminal state.
 */
class VibeCutPlanRuntime : public QObject
{
    Q_OBJECT
public:
    explicit VibeCutPlanRuntime(VibeCutToolSurface *surface, QObject *parent = nullptr);

    QJsonObject propose(const QJsonObject &proposal);
    QJsonObject proposeDirectToolCalls(const QJsonArray &toolUseBlocks, const QString &objective = QString());
    QJsonObject approvePendingPlan();
    QJsonObject cancelPendingPlan();

    void setTrustMode(VibeCutTrustMode mode) { m_trustMode = mode; }
    VibeCutTrustMode trustMode() const { return m_trustMode; }
    bool pendingRequiresConfirmation() const;

    bool hasPendingPlan() const { return m_hasPending; }
    bool executing() const { return m_executing; }
    QString pendingPlanId() const { return m_hasPending ? m_plan.id : QString(); }
    QJsonObject pendingPlanJson() const { return m_hasPending ? m_plan.toJson() : QJsonObject(); }
    QString pendingPlanSummary() const;

Q_SIGNALS:
    void planProposed(const QString &planId, const QString &summary);
    void planApproved(const QString &planId);
    void planProgress(const QString &message);
    void planFinished(const QString &planId, bool success, const QString &summary, const QJsonArray &operationResults);

private Q_SLOTS:
    void onJobChanged(const QString &jobId);

private:
    bool setPendingPlan(const VibeCutEditPlan &plan, QString &error);
    void continueExecution();
    void finishExecution(bool success, const QString &summary);
    const VibeCutPlanOperation *operationById(const QString &id) const;
    void beginCheckpointMacro();
    void closeCheckpointMacro();
    void rollbackCheckpointMacro();

    VibeCutToolSurface *m_surface = nullptr;
    VibeCutEditPlan m_plan;
    VibeCutTrustMode m_trustMode = VibeCutTrustMode::Off;
    bool m_hasPending = false;
    bool m_executing = false;
    bool m_macroOpen = false;
    QStringList m_executionOrder;
    int m_executionIndex = 0;
    quint64 m_expectedRevision = 0;
    QString m_waitingJobId;
    VibeCutToolPolicy m_waitingPolicy;
    QJsonArray m_operationResults;
    VibeCutProjectSnapshot m_beforeSnapshot;
};
