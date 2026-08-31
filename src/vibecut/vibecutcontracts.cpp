/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecutcontracts.h"

#include <QJsonArray>
#include <QSet>

namespace {
QString riskName(VibeCutToolRisk risk)
{
    switch (risk) {
    case VibeCutToolRisk::ReadOnly: return QStringLiteral("read_only");
    case VibeCutToolRisk::ReversibleEdit: return QStringLiteral("reversible_edit");
    case VibeCutToolRisk::MajorEdit: return QStringLiteral("major_edit");
    case VibeCutToolRisk::ExternalSideEffect: return QStringLiteral("external_side_effect");
    case VibeCutToolRisk::Irreversible: return QStringLiteral("irreversible");
    }
    return QStringLiteral("read_only");
}

QJsonArray stringArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) array.append(value);
    return array;
}

QStringList stringList(const QJsonValue &value)
{
    QStringList result;
    for (const QJsonValue &entry : value.toArray()) result.append(entry.toString());
    return result;
}

bool visitOperation(const QString &id, const QHash<QString, QStringList> &dependencies, QSet<QString> &visiting, QSet<QString> &visited)
{
    if (visited.contains(id)) return true;
    if (visiting.contains(id)) return false;
    visiting.insert(id);
    for (const QString &dependency : dependencies.value(id)) {
        if (!visitOperation(dependency, dependencies, visiting, visited)) return false;
    }
    visiting.remove(id);
    visited.insert(id);
    return true;
}
} // namespace

bool VibeCutToolPolicy::requiresConfirmation(VibeCutTrustMode mode) const
{
    if (risk == VibeCutToolRisk::ReadOnly) return false;
    if (risk == VibeCutToolRisk::Irreversible || confirmationRequired) return true;
    if (autoAllowed) return false;
    if (mode == VibeCutTrustMode::Off) return true;
    if (mode == VibeCutTrustMode::Auto) {
        return risk == VibeCutToolRisk::MajorEdit || risk == VibeCutToolRisk::ExternalSideEffect;
    }
    return false;
}

QJsonObject VibeCutToolPolicy::toJson() const
{
    return QJsonObject{{QStringLiteral("name"), name},
                       {QStringLiteral("risk"), riskName(risk)},
                       {QStringLiteral("reversible"), reversible},
                       {QStringLiteral("mutates_project"), mutatesProject},
                       {QStringLiteral("async"), asynchronous},
                       {QStringLiteral("confirmation_required"), confirmationRequired},
                       {QStringLiteral("auto_allowed"), autoAllowed},
                       {QStringLiteral("enabled"), enabled}};
}

QJsonObject VibeCutPlanOperation::toJson() const
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("tool"), toolName},
                       {QStringLiteral("input"), input},
                       {QStringLiteral("depends_on"), stringArray(dependsOn)},
                       {QStringLiteral("expected_postconditions"), stringArray(expectedPostconditions)}};
}

VibeCutPlanOperation VibeCutPlanOperation::fromJson(const QJsonObject &object)
{
    VibeCutPlanOperation operation;
    operation.id = object.value(QStringLiteral("id")).toString();
    operation.toolName = object.value(QStringLiteral("tool")).toString();
    operation.input = object.value(QStringLiteral("input")).toObject();
    operation.dependsOn = stringList(object.value(QStringLiteral("depends_on")));
    operation.expectedPostconditions = stringList(object.value(QStringLiteral("expected_postconditions")));
    return operation;
}

QJsonObject VibeCutEditPlan::toJson() const
{
    QJsonArray operationArray;
    for (const VibeCutPlanOperation &operation : operations) operationArray.append(operation.toJson());
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)},
                       {QStringLiteral("objective"), objective},
                       {QStringLiteral("operations"), operationArray}};
}

VibeCutEditPlan VibeCutEditPlan::fromJson(const QJsonObject &object)
{
    VibeCutEditPlan plan;
    plan.id = object.value(QStringLiteral("id")).toString();
    plan.baseRevision = static_cast<quint64>(object.value(QStringLiteral("base_revision")).toVariant().toULongLong());
    plan.objective = object.value(QStringLiteral("objective")).toString();
    for (const QJsonValue &entry : object.value(QStringLiteral("operations")).toArray()) {
        plan.operations.append(VibeCutPlanOperation::fromJson(entry.toObject()));
    }
    return plan;
}

VibeCutPlanValidation VibeCutEditPlan::validate() const
{
    VibeCutPlanValidation result;
    if (id.trimmed().isEmpty()) result.errors.append(QStringLiteral("plan id is required"));
    if (objective.trimmed().isEmpty()) result.errors.append(QStringLiteral("plan objective is required"));
    if (operations.isEmpty()) result.errors.append(QStringLiteral("plan must contain at least one operation"));

    QSet<QString> ids;
    QHash<QString, QStringList> dependencies;
    for (const VibeCutPlanOperation &operation : operations) {
        if (operation.id.trimmed().isEmpty()) {
            result.errors.append(QStringLiteral("operation id is required"));
            continue;
        }
        if (ids.contains(operation.id)) result.errors.append(QStringLiteral("duplicate operation id: %1").arg(operation.id));
        ids.insert(operation.id);
        if (operation.toolName.trimmed().isEmpty()) result.errors.append(QStringLiteral("operation %1 has no tool").arg(operation.id));
        dependencies.insert(operation.id, operation.dependsOn);
    }

    for (const VibeCutPlanOperation &operation : operations) {
        for (const QString &dependency : operation.dependsOn) {
            if (dependency == operation.id) result.errors.append(QStringLiteral("operation %1 depends on itself").arg(operation.id));
            else if (!ids.contains(dependency)) result.errors.append(QStringLiteral("operation %1 depends on unknown operation %2").arg(operation.id, dependency));
        }
    }

    QSet<QString> visiting;
    QSet<QString> visited;
    for (const QString &operationId : ids) {
        if (!visitOperation(operationId, dependencies, visiting, visited)) {
            result.errors.append(QStringLiteral("operation dependency graph contains a cycle"));
            break;
        }
    }

    result.ok = result.errors.isEmpty();
    return result;
}

bool VibeCutEditPlan::matchesRevision(quint64 currentRevision) const
{
    return baseRevision == currentRevision;
}

bool VibeCutEditPlan::requiresConfirmation(const QHash<QString, VibeCutToolPolicy> &policies, VibeCutTrustMode mode) const
{
    for (const VibeCutPlanOperation &operation : operations) {
        const auto policy = policies.constFind(operation.toolName);
        if (policy == policies.constEnd() || !policy.value().enabled) return true;
        if (policy.value().requiresConfirmation(mode)) return true;
    }
    return false;
}
