/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecutplanruntime.h"

#include "core.h"
#include "doc/kdenlivedoc.h"
#include "doc/docundostack.hpp"
#include "vibecutjobmanager.h"
#include "vibecutplangate.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QUuid>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString riskLabel(VibeCutToolRisk risk)
{
    switch (risk) {
    case VibeCutToolRisk::ReadOnly: return QStringLiteral("read");
    case VibeCutToolRisk::ReversibleEdit: return QStringLiteral("reversible edit");
    case VibeCutToolRisk::MajorEdit: return QStringLiteral("major edit");
    case VibeCutToolRisk::ExternalSideEffect: return QStringLiteral("external side effect");
    case VibeCutToolRisk::Irreversible: return QStringLiteral("irreversible");
    }
    return QStringLiteral("unknown");
}

std::shared_ptr<DocUndoStack> currentUndoStack()
{
    if (!pCore || !pCore->currentDoc()) return std::shared_ptr<DocUndoStack>();
    return pCore->currentDoc()->commandStack();
}
} // namespace

VibeCutPlanRuntime::VibeCutPlanRuntime(VibeCutToolSurface *surface, QObject *parent)
    : QObject(parent)
    , m_surface(surface)
{
    if (m_surface && m_surface->baseTools()) {
        connect(m_surface->baseTools()->jobManager(), &VibeCutJobManager::jobChanged, this, &VibeCutPlanRuntime::onJobChanged);
    }
}

QJsonObject VibeCutPlanRuntime::propose(const QJsonObject &proposal)
{
    if (!m_surface) return err(QStringLiteral("Plan runtime has no tool surface."));
    VibeCutEditPlan plan;
    plan.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    plan.baseRevision = m_surface->projectRevision();
    plan.objective = proposal.value(QStringLiteral("objective")).toString().trimmed();
    for (const QJsonValue &value : proposal.value(QStringLiteral("operations")).toArray()) {
        plan.operations.append(VibeCutPlanOperation::fromJson(value.toObject()));
    }
    QString error;
    if (!setPendingPlan(plan, error)) return err(error);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("awaiting_approval"), pendingRequiresConfirmation()},
                       {QStringLiteral("plan"), plan.toJson()}};
}

QJsonObject VibeCutPlanRuntime::proposeDirectToolCalls(const QJsonArray &toolUseBlocks, const QString &objective)
{
    if (!m_surface) return err(QStringLiteral("Plan runtime has no tool surface."));
    VibeCutEditPlan plan;
    plan.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    plan.baseRevision = m_surface->projectRevision();
    plan.objective = objective.trimmed().isEmpty() ? QStringLiteral("Review requested editor changes") : objective.trimmed();

    const QHash<QString, VibeCutToolPolicy> policies = m_surface->policies();
    QString previousId;
    int step = 0;
    for (const QJsonValue &value : toolUseBlocks) {
        const QJsonObject block = value.toObject();
        if (block.value(QStringLiteral("type")).toString() != QLatin1String("tool_use")) continue;
        const QString name = block.value(QStringLiteral("name")).toString();
        const auto policy = policies.constFind(name);
        if (policy != policies.constEnd() && policy.value().risk == VibeCutToolRisk::ReadOnly) continue;
        VibeCutPlanOperation operation;
        operation.id = QStringLiteral("step-%1").arg(++step);
        operation.toolName = name;
        operation.input = block.value(QStringLiteral("input")).toObject();
        if (!previousId.isEmpty()) operation.dependsOn.append(previousId);
        plan.operations.append(operation);
        previousId = operation.id;
    }
    if (plan.operations.isEmpty()) return err(QStringLiteral("No mutating or side-effecting tool calls were present to review."));
    QString error;
    if (!setPendingPlan(plan, error)) return err(error);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("awaiting_approval"), pendingRequiresConfirmation()},
                       {QStringLiteral("plan"), plan.toJson()}};
}

bool VibeCutPlanRuntime::setPendingPlan(const VibeCutEditPlan &plan, QString &error)
{
    if (m_executing || m_hasPending) {
        error = QStringLiteral("Another VibeCut plan is already pending or executing.");
        return false;
    }
    const VibeCutPlanValidation validation = plan.validate();
    if (!validation.ok) {
        error = validation.errors.join(QStringLiteral("; "));
        return false;
    }

    const QHash<QString, VibeCutToolPolicy> policies = m_surface->policies();
    bool hasEffect = false;
    for (const VibeCutPlanOperation &operation : plan.operations) {
        if (operation.toolName == QLatin1String("edit_plan_propose")) {
            error = QStringLiteral("A plan cannot recursively contain edit_plan_propose.");
            return false;
        }
        const auto policy = policies.constFind(operation.toolName);
        if (policy == policies.constEnd()) {
            error = QStringLiteral("Plan references unknown or ungoverned tool: %1").arg(operation.toolName);
            return false;
        }
        if (policy.value().risk != VibeCutToolRisk::ReadOnly) hasEffect = true;
    }
    if (!hasEffect) {
        error = QStringLiteral("A review plan must contain at least one edit or external side effect; read-only inspection can run immediately.");
        return false;
    }

    m_plan = plan;
    m_hasPending = true;
    m_planMutatesProject = false;
    m_executionOrder.clear();
    m_executionIndex = 0;
    m_waitingJobId.clear();
    m_operationResults = QJsonArray();
    Q_EMIT planProposed(m_plan.id, pendingPlanSummary());
    return true;
}

bool VibeCutPlanRuntime::pendingRequiresConfirmation() const
{
    return m_hasPending && m_surface && m_plan.requiresConfirmation(m_surface->policies(), m_trustMode);
}

QJsonObject VibeCutPlanRuntime::approvePendingPlan()
{
    if (!m_hasPending || m_executing) return err(QStringLiteral("There is no reviewable VibeCut plan awaiting approval."));
    if (!m_surface) return err(QStringLiteral("Plan runtime has no tool surface."));

    const QHash<QString, VibeCutToolPolicy> policies = m_surface->policies();
    const VibeCutPlanGateResult gate = VibeCutPlanGate::assess(m_plan, m_surface->projectRevision(), policies, m_trustMode, true);
    if (!gate.ready()) {
        const QString message = gate.errors.join(QStringLiteral("; "));
        finishExecution(false, message);
        return err(message);
    }

    m_planMutatesProject = false;
    for (const VibeCutPlanOperation &operation : m_plan.operations) {
        const auto policy = policies.constFind(operation.toolName);
        if (policy != policies.constEnd() && policy.value().mutatesProject) {
            m_planMutatesProject = true;
            break;
        }
    }

    m_executionOrder = gate.executionOrder;
    m_executionIndex = 0;
    m_expectedRevision = m_surface->projectRevision();
    m_beforeSnapshot = m_planMutatesProject ? VibeCutProjectSnapshot::capture(m_expectedRevision) : VibeCutProjectSnapshot();
    m_executing = true;
    Q_EMIT planApproved(m_plan.id);
    Q_EMIT planProgress(QStringLiteral("Executing approved plan %1…").arg(m_plan.id));
    continueExecution();

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("plan_id"), m_plan.id},
                       {QStringLiteral("executing"), m_executing}, {QStringLiteral("waiting_job_id"), m_waitingJobId}};
}

QJsonObject VibeCutPlanRuntime::cancelPendingPlan()
{
    if (!m_hasPending) return err(QStringLiteral("There is no pending plan to cancel."));
    if (m_executing) return err(QStringLiteral("The plan is already executing; cancellation of active editor operations is not yet supported."));
    const QString planId = m_plan.id;
    m_hasPending = false;
    m_planMutatesProject = false;
    m_plan = VibeCutEditPlan();
    m_executionOrder.clear();
    m_operationResults = QJsonArray();
    Q_EMIT planFinished(planId, false, QStringLiteral("Plan cancelled before execution."), QJsonArray());
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("cancelled"), true}, {QStringLiteral("plan_id"), planId}};
}

QString VibeCutPlanRuntime::pendingPlanSummary() const
{
    if (!m_hasPending) return QString();
    QStringList lines;
    lines.append(QStringLiteral("Plan: %1").arg(m_plan.objective));
    lines.append(QStringLiteral("Base project revision: %1").arg(m_plan.baseRevision));
    const QHash<QString, VibeCutToolPolicy> policies = m_surface ? m_surface->policies() : QHash<QString, VibeCutToolPolicy>();
    int index = 0;
    for (const VibeCutPlanOperation &operation : m_plan.operations) {
        const auto policy = policies.constFind(operation.toolName);
        const QString risk = policy == policies.constEnd() ? QStringLiteral("unknown") : riskLabel(policy.value().risk);
        lines.append(QStringLiteral("%1. %2 [%3]").arg(++index).arg(operation.toolName, risk));
    }
    return lines.join(QLatin1Char('\n'));
}

const VibeCutPlanOperation *VibeCutPlanRuntime::operationById(const QString &id) const
{
    for (const VibeCutPlanOperation &operation : m_plan.operations) {
        if (operation.id == id) return &operation;
    }
    return nullptr;
}

void VibeCutPlanRuntime::beginCheckpointMacro()
{
    if (m_macroOpen) return;
    const std::shared_ptr<DocUndoStack> stack = currentUndoStack();
    if (!stack) return;
    m_macroStartIndex = stack->index();
    stack->beginMacro(QStringLiteral("VibeCut: %1").arg(m_plan.objective.left(80)));
    m_macroOpen = true;
}

void VibeCutPlanRuntime::closeCheckpointMacro()
{
    if (!m_macroOpen) return;
    const std::shared_ptr<DocUndoStack> stack = currentUndoStack();
    if (stack) stack->endMacro();
    m_macroOpen = false;
    m_macroStartIndex = -1;
}

void VibeCutPlanRuntime::rollbackCheckpointMacro()
{
    if (!m_macroOpen) return;
    const std::shared_ptr<DocUndoStack> stack = currentUndoStack();
    if (!stack) {
        m_macroOpen = false;
        m_macroStartIndex = -1;
        return;
    }
    const int targetIndex = m_macroStartIndex;
    stack->endMacro();
    m_macroOpen = false;
    m_macroStartIndex = -1;
    if (targetIndex >= 0 && stack->index() >= targetIndex) {
        while (stack->index() > targetIndex && stack->canUndo()) stack->undo();
    }
    if (m_surface) m_expectedRevision = m_surface->projectRevision();
}

void VibeCutPlanRuntime::continueExecution()
{
    if (!m_executing || !m_surface || !m_waitingJobId.isEmpty()) return;

    const QHash<QString, VibeCutToolPolicy> policies = m_surface->policies();
    while (m_executionIndex < m_executionOrder.size()) {
        if (!m_macroOpen && m_surface->projectRevision() != m_expectedRevision) {
            finishExecution(false, QStringLiteral("The project changed during plan execution; remaining operations were stopped."));
            return;
        }

        const QString operationId = m_executionOrder.at(m_executionIndex);
        const VibeCutPlanOperation *operation = operationById(operationId);
        if (!operation) {
            finishExecution(false, QStringLiteral("Execution order references missing operation %1.").arg(operationId));
            return;
        }
        const auto policy = policies.constFind(operation->toolName);
        if (policy == policies.constEnd()) {
            finishExecution(false, QStringLiteral("Tool %1 lost its governance policy before execution.").arg(operation->toolName));
            return;
        }

        const bool projectMutation = policy.value().mutatesProject;
        if (projectMutation) beginCheckpointMacro();
        Q_EMIT planProgress(QStringLiteral("Step %1/%2: %3…").arg(m_executionIndex + 1).arg(m_executionOrder.size()).arg(operation->toolName));
        const QJsonObject result = m_surface->invoke(operation->toolName, operation->input);
        m_operationResults.append(QJsonObject{{QStringLiteral("operation_id"), operation->id},
                                              {QStringLiteral("tool"), operation->toolName},
                                              {QStringLiteral("result"), result}});
        if (!result.value(QStringLiteral("ok")).toBool()) {
            if (m_macroOpen) rollbackCheckpointMacro();
            finishExecution(false, QStringLiteral("Step %1 failed%2: %3")
                                        .arg(operation->toolName,
                                             projectMutation ? QStringLiteral(" and the current synchronous project checkpoint was rolled back") : QString(),
                                             result.value(QStringLiteral("error")).toString(QStringLiteral("unknown error"))));
            return;
        }

        if (result.value(QStringLiteral("started")).toBool()) {
            closeCheckpointMacro();
            m_expectedRevision = m_surface->projectRevision();
            const QString jobId = result.value(QStringLiteral("job_id")).toString();
            if (jobId.isEmpty()) {
                finishExecution(false, QStringLiteral("Step %1 started asynchronously without a trackable job id; the plan was stopped rather than guessing when to continue.")
                                           .arg(operation->toolName));
                return;
            }
            m_waitingJobId = jobId;
            m_waitingPolicy = policy.value();
            Q_EMIT planProgress(QStringLiteral("Waiting for background job %1…").arg(jobId));
            return;
        }

        ++m_executionIndex;
    }

    closeCheckpointMacro();
    m_expectedRevision = m_surface->projectRevision();
    finishExecution(true, QStringLiteral("Approved plan completed and every checkpoint reported success."));
}

void VibeCutPlanRuntime::onJobChanged(const QString &jobId)
{
    if (!m_executing || jobId != m_waitingJobId || !m_surface || !m_surface->baseTools()) return;

    VibeCutJob job;
    if (!m_surface->baseTools()->jobManager()->job(jobId, job) || !job.terminal()) return;
    if (job.state != VibeCutJobState::Succeeded) {
        finishExecution(false, QStringLiteral("Background step failed: %1").arg(job.message));
        return;
    }

    const bool projectChanged = m_surface->projectRevision() != m_expectedRevision;
    const bool externalOnlyJob = !m_waitingPolicy.mutatesProject;
    m_expectedRevision = m_surface->projectRevision();
    m_waitingJobId.clear();
    ++m_executionIndex;

    if (externalOnlyJob && projectChanged && m_executionIndex < m_executionOrder.size()) {
        finishExecution(false, QStringLiteral("The external background step completed, but the project changed while it was running; remaining plan operations were stopped as stale."));
        return;
    }
    // If an external-only job was the final operation (for example render), a
    // later user edit does not invalidate the already-produced external output.
    continueExecution();
}

void VibeCutPlanRuntime::finishExecution(bool success, const QString &summary)
{
    closeCheckpointMacro();
    const QString planId = m_plan.id;
    QJsonArray results = m_operationResults;
    QString finalSummary = summary;
    if (m_planMutatesProject && m_surface && m_beforeSnapshot.available) {
        const VibeCutProjectSnapshot after = VibeCutProjectSnapshot::capture(m_surface->projectRevision());
        if (after.available) {
            const VibeCutProjectDiff diff = m_beforeSnapshot.diffTo(after);
            results.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("project_diff")},
                                       {QStringLiteral("before"), m_beforeSnapshot.toJson()},
                                       {QStringLiteral("after"), after.toJson()},
                                       {QStringLiteral("diff"), diff.toJson()}});
            finalSummary += QStringLiteral(" %1.").arg(diff.summary());
        }
    }

    m_executing = false;
    m_hasPending = false;
    m_macroOpen = false;
    m_macroStartIndex = -1;
    m_planMutatesProject = false;
    m_executionOrder.clear();
    m_executionIndex = 0;
    m_waitingJobId.clear();
    m_plan = VibeCutEditPlan();
    m_operationResults = QJsonArray();
    m_beforeSnapshot = VibeCutProjectSnapshot();
    Q_EMIT planFinished(planId, success, finalSummary, results);
}
