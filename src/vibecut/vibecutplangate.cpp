/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecutplangate.h"

#include <QSet>

namespace {
QStringList orderedOperations(const VibeCutEditPlan &plan)
{
    QStringList result;
    QSet<QString> complete;

    while (result.size() < plan.operations.size()) {
        bool progressed = false;
        for (const VibeCutPlanOperation &operation : plan.operations) {
            if (complete.contains(operation.id)) {
                continue;
            }
            bool dependenciesComplete = true;
            for (const QString &dependency : operation.dependsOn) {
                if (!complete.contains(dependency)) {
                    dependenciesComplete = false;
                    break;
                }
            }
            if (dependenciesComplete) {
                result.append(operation.id);
                complete.insert(operation.id);
                progressed = true;
            }
        }
        if (!progressed) {
            return QStringList();
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
        if (!policies.contains(operation.toolName)) {
            result.status = VibeCutPlanGateStatus::UnknownTool;
            result.errors.append(QStringLiteral("plan references unknown or ungoverned tool: %1").arg(operation.toolName));
        }
    }
    if (result.status == VibeCutPlanGateStatus::UnknownTool) {
        return result;
    }

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
