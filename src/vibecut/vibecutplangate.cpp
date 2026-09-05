/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecutplangate.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace {
QStringList orderedOperations(const VibeCutEditPlan &plan)
{
    QHash<QString, QStringList> dependencies;
    QSet<QString> remaining;
    for (const VibeCutPlanOperation &operation : plan.operations) {
        dependencies.insert(operation.id, operation.dependsOn);
        remaining.insert(operation.id);
    }

    QStringList result;
    QSet<QString> complete;
    while (!remaining.isEmpty()) {
        QStringList ready;
        for (const QString &operationId : remaining) {
            bool dependenciesComplete = true;
            for (const QString &dependency : dependencies.value(operationId)) {
                if (!complete.contains(dependency)) {
                    dependenciesComplete = false;
                    break;
                }
            }
            if (dependenciesComplete) ready.append(operationId);
        }
        if (ready.isEmpty()) return QStringList();
        std::sort(ready.begin(), ready.end());
        for (const QString &operationId : ready) {
            result.append(operationId);
            complete.insert(operationId);
            remaining.remove(operationId);
        }
    }
    return result;
}
} // namespace

VibeCutPlanGateResult VibeCutPlanGate::assess(const VibeCutEditPlan &plan, quint64 currentRevision,
                                              const QHash<QString, VibeCutToolPolicy> &policies, VibeCutTrustMode mode,
                                              bool planApproved)
{
    VibeCutPlanGateResult result;

    const VibeCutPlanValidation validation = plan.validate();
    if (!validation.ok) {
        result.status = VibeCutPlanGateStatus::InvalidPlan;
        result.errors = validation.errors;
        return result;
    }

    if (!plan.matchesRevision(currentRevision)) {
        result.status = VibeCutPlanGateStatus::StalePlan;
        result.errors.append(QStringLiteral("plan was created for revision %1 but current revision is %2")
                                 .arg(plan.baseRevision)
                                 .arg(currentRevision));
        return result;
    }

    for (const VibeCutPlanOperation &operation : plan.operations) {
        const auto policy = policies.constFind(operation.toolName);
        if (policy == policies.constEnd()) {
            result.status = VibeCutPlanGateStatus::UnknownTool;
            result.errors.append(QStringLiteral("plan references unknown or ungoverned tool: %1").arg(operation.toolName));
            continue;
        }
        if (!policy.value().enabled) {
            result.status = VibeCutPlanGateStatus::ToolDenied;
            result.errors.append(QStringLiteral("tool '%1' is denied by project policy").arg(operation.toolName));
        }
    }
    if (result.status == VibeCutPlanGateStatus::UnknownTool || result.status == VibeCutPlanGateStatus::ToolDenied) return result;

    if (!planApproved && plan.requiresConfirmation(policies, mode)) {
        result.status = VibeCutPlanGateStatus::ConfirmationRequired;
        result.errors.append(QStringLiteral("plan contains work that requires confirmation in the current trust mode"));
        return result;
    }

    result.executionOrder = orderedOperations(plan);
    if (result.executionOrder.size() != plan.operations.size()) {
        result.status = VibeCutPlanGateStatus::InvalidPlan;
        result.errors.append(QStringLiteral("could not derive a complete execution order"));
        return result;
    }

    result.status = VibeCutPlanGateStatus::Ready;
    return result;
}
