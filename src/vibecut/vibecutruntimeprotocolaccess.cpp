/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutruntimeprotocoladapter.h"

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
