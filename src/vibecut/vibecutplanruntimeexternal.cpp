/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutplanruntime.h"

QJsonObject VibeCutPlanRuntime::resolvePendingPlanExternally(const QString &planId,
                                                             bool success,
                                                             const QString &summary,
                                                             const QJsonObject &externalResult)
{
    if (!m_hasPending || m_executing) {
        return QJsonObject{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"), QStringLiteral("There is no externally resolvable pending plan.")}};
    }
    const QString id = planId.trimmed();
    if (id.isEmpty() || id != m_plan.id) {
        return QJsonObject{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"), QStringLiteral("External runtime completion does not match the pending plan id.")}};
    }

    QJsonArray results;
    if (!externalResult.isEmpty()) {
        results.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("external_runtime_result")},
                                   {QStringLiteral("result"), externalResult}});
    }

    m_hasPending = false;
    m_executing = false;
    m_macroOpen = false;
    m_macroStartIndex = -1;
    m_planMutatesProject = false;
    m_executionOrder.clear();
    m_executionIndex = 0;
    m_expectedRevision = 0;
    m_waitingJobId.clear();
    m_plan = VibeCutEditPlan();
    m_operationResults = QJsonArray();
    m_beforeSnapshot = VibeCutProjectSnapshot();

    const QString finalSummary = summary.trimmed().isEmpty()
                                     ? (success ? QStringLiteral("External runtime plan completed.")
                                                : QStringLiteral("External runtime plan stopped."))
                                     : summary.trimmed();
    Q_EMIT planFinished(id, success, finalSummary, results);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("plan_id"), id},
                       {QStringLiteral("resolved"), true},
                       {QStringLiteral("success"), success}};
}
