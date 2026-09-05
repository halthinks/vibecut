/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutruntimeprotocoladapter.h"

#include "vibecuttoolsurface.h"

#include <cmath>

namespace {
constexpr qint64 MaxExactJsonInteger = 9007199254740991LL;

bool exactRevisionValue(const QJsonValue &value, quint64 &revision)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < 0.0 || number > static_cast<double>(MaxExactJsonInteger)) return false;
    revision = static_cast<quint64>(number);
    return true;
}

bool fail(QString *code, QString *error, const QString &errorCode, const QString &message)
{
    if (code) *code = errorCode;
    if (error) *error = message;
    return false;
}
} // namespace

bool VibeCutRuntimeProtocolAdapter::approvedOperationPolicy(const QString &operationId,
                                                            VibeCutToolPolicy &policy,
                                                            QString *error) const
{
    if (error) error->clear();
    if (!m_hasPlan || m_authorizationId.isEmpty()) {
        if (error) *error = QStringLiteral("There is no active authorized protocol plan.");
        return false;
    }
    const QString id = operationId.trimmed();
    if (id.isEmpty() || !m_approvedOperationIds.contains(id)) {
        if (error) *error = QStringLiteral("Operation is not part of the active approved plan.");
        return false;
    }
    const auto it = m_authorizedPolicies.constFind(id);
    if (it == m_authorizedPolicies.constEnd()) {
        if (error) *error = QStringLiteral("Authorized operation policy is unavailable.");
        return false;
    }
    policy = it.value();
    return true;
}

bool VibeCutRuntimeProtocolAdapter::preflightInvoke(const QJsonObject &request,
                                                    VibeCutToolPolicy &policy,
                                                    QString *errorCode,
                                                    QString *error) const
{
    if (errorCode) errorCode->clear();
    if (error) error->clear();
    if (!m_surface) return fail(errorCode, error, QStringLiteral("adapter_unavailable"), QStringLiteral("VibeCutToolSurface is unavailable."));
    if (request.value(QStringLiteral("v")).toInt(-1) != 1 ||
        request.value(QStringLiteral("kind")).toString() != QLatin1String("request") ||
        request.value(QStringLiteral("type")).toString() != QLatin1String("invoke") ||
        !request.value(QStringLiteral("payload")).isObject()) {
        return fail(errorCode, error, QStringLiteral("invalid_invoke"), QStringLiteral("Invoke preflight requires a v1 invoke request envelope."));
    }

    const QJsonObject payload = request.value(QStringLiteral("payload")).toObject();
    if (payload.contains(QStringLiteral("tool")) || payload.contains(QStringLiteral("input"))) {
        return fail(errorCode, error, QStringLiteral("plan_substitution_attempt"),
                    QStringLiteral("invoke may not supply tool or input; adapter resolves the exact approved operation."));
    }
    const QString planId = payload.value(QStringLiteral("plan_id")).toString().trimmed();
    const QString authorization = payload.value(QStringLiteral("authorization_id")).toString().trimmed();
    const QString operationId = payload.value(QStringLiteral("operation_id")).toString().trimmed();
    quint64 requestedRevision = 0;
    if (!exactRevisionValue(payload.value(QStringLiteral("expected_revision")), requestedRevision) ||
        planId.isEmpty() || authorization.isEmpty() || operationId.isEmpty()) {
        return fail(errorCode, error, QStringLiteral("invalid_invoke"),
                    QStringLiteral("invoke requires plan_id, authorization_id, operation_id and exact expected_revision."));
    }
    if (!m_hasPlan || planId != m_plan.id || authorization != m_authorizationId || m_authorizationId.isEmpty()) {
        return fail(errorCode, error, QStringLiteral("invalid_authorization"),
                    QStringLiteral("invoke does not match the active approved plan authorization."));
    }
    if (!m_waitingJobs.isEmpty()) {
        return fail(errorCode, error, QStringLiteral("job_pending"),
                    QStringLiteral("A protocol plan operation is still waiting on a background job."));
    }
    if (requestedRevision != m_expectedRevision || currentRevision() != m_expectedRevision) {
        return fail(errorCode, error, QStringLiteral("stale_revision"),
                    QStringLiteral("expected_revision no longer matches adapter project revision."));
    }
    if (!m_approvedOperationIds.contains(operationId) || m_completedOperationIds.contains(operationId)) {
        return fail(errorCode, error, QStringLiteral("operation_not_approved"),
                    QStringLiteral("Operation is not approved or has already completed."));
    }
    if (nextOperationId() != operationId) {
        return fail(errorCode, error, QStringLiteral("operation_out_of_order"),
                    QStringLiteral("Operation is not the next approved dependency-ordered step."));
    }
    const VibeCutPlanOperation *operation = operationById(operationId);
    if (!operation) {
        return fail(errorCode, error, QStringLiteral("missing_operation"), QStringLiteral("Stored approved operation is missing."));
    }
    QString dependencyError;
    if (!operationDependenciesComplete(*operation, &dependencyError)) {
        return fail(errorCode, error, QStringLiteral("dependency_incomplete"), dependencyError);
    }

    const QHash<QString, VibeCutToolPolicy> policies = m_surface->policies();
    const auto currentPolicy = policies.constFind(operation->toolName);
    const auto approvedPolicy = m_authorizedPolicies.constFind(operationId);
    if (currentPolicy == policies.constEnd() || approvedPolicy == m_authorizedPolicies.constEnd() ||
        !currentPolicy.value().enabled || currentPolicy.value().toJson() != approvedPolicy.value().toJson()) {
        return fail(errorCode, error, QStringLiteral("policy_changed"),
                    QStringLiteral("Effective tool policy changed after authorization; plan must be re-proposed/re-authorized."));
    }
    policy = currentPolicy.value();
    return true;
}

QString VibeCutRuntimeProtocolAdapter::activePlanObjective() const
{
    return m_hasPlan ? m_plan.objective : QString();
}

quint64 VibeCutRuntimeProtocolAdapter::protocolProjectRevision() const
{
    return currentRevision();
}

quint64 VibeCutRuntimeProtocolAdapter::synchronizeExpectedRevision()
{
    const quint64 revision = currentRevision();
    if (m_hasPlan && !m_authorizationId.isEmpty()) m_expectedRevision = revision;
    return revision;
}

bool VibeCutRuntimeProtocolAdapter::ownsProtocolJob(const QString &jobId) const
{
    const QString id = jobId.trimmed();
    return !id.isEmpty() && m_waitingJobs.contains(id);
}
