/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecutagent.h"

#include "vibecutplanruntime.h"
#include "vibecutruntimeprotocoladapter.h"
#include "vibecutruntimestdiotransport.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDebug>

namespace {
bool parseRuntimeArguments(const QString &raw, QStringList &arguments, QString *error)
{
    if (error) error->clear();
    arguments.clear();
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) return true;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) *error = QStringLiteral("VIBECUT_EXTERNAL_RUNTIME_ARGS_JSON must be a JSON array of strings: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonArray values = document.array();
    if (values.size() > 64) {
        if (error) *error = QStringLiteral("VIBECUT_EXTERNAL_RUNTIME_ARGS_JSON exceeds the 64-argument bound.");
        return false;
    }
    for (const QJsonValue &value : values) {
        if (!value.isString() || value.toString().size() > 4096) {
            if (error) *error = QStringLiteral("Every external runtime argument must be a string no longer than 4096 characters.");
            arguments.clear();
            return false;
        }
        arguments.append(value.toString());
    }
    return true;
}
} // namespace

void VibeCutAgent::initializeExternalRuntime()
{
    m_externalRuntimeProgram = qEnvironmentVariable("VIBECUT_EXTERNAL_RUNTIME_PROGRAM").trimmed();
    if (m_externalRuntimeProgram.isEmpty()) return;

    if (!parseRuntimeArguments(qEnvironmentVariable("VIBECUT_EXTERNAL_RUNTIME_ARGS_JSON"),
                               m_externalRuntimeArguments, &m_externalRuntimeError)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] external runtime configuration rejected: %1").arg(m_externalRuntimeError);
        return;
    }

    m_externalProtocolAdapter = new VibeCutRuntimeProtocolAdapter(m_toolSurface, VibeCutRuntimeProtocolAdapter::RevisionProvider(), this);
    m_externalRuntimeTransport = new VibeCutRuntimeStdioTransport(m_externalProtocolAdapter, this);

    connect(m_externalRuntimeTransport, &VibeCutRuntimeStdioTransport::diagnostic, this, [this](const QString &message) {
        qWarning().noquote() << QStringLiteral("[VibeCut external runtime] %1").arg(message);
        Q_EMIT backgroundProgress(QStringLiteral("External runtime: %1").arg(message));
    });
    connect(m_externalRuntimeTransport, &VibeCutRuntimeStdioTransport::hostedPlanFinished,
            this, &VibeCutAgent::resolveHostedPlan);
    connect(m_externalRuntimeTransport, &VibeCutRuntimeStdioTransport::stopped, this,
            [this](int exitCode, int exitStatus) {
        Q_UNUSED(exitStatus)
        if (!m_externalPlanExecuting) return;
        m_externalRuntimeError = QStringLiteral("External runtime process stopped with exit code %1.").arg(exitCode);
    });

    QString startError;
    if (!ensureExternalRuntimeReady(&startError)) {
        m_externalRuntimeError = startError;
        qWarning().noquote() << QStringLiteral("[VibeCut] external runtime unavailable: %1").arg(startError);
    }
}

bool VibeCutAgent::ensureExternalRuntimeReady(QString *error)
{
    if (error) error->clear();
    if (!externalRuntimeRequested()) {
        if (error) *error = QStringLiteral("No external runtime program is configured.");
        return false;
    }
    if (!m_externalProtocolAdapter || !m_externalRuntimeTransport) {
        if (error) *error = m_externalRuntimeError.isEmpty()
                                ? QStringLiteral("External runtime configuration could not be initialized.")
                                : m_externalRuntimeError;
        return false;
    }

    QString readyError;
    if (!m_externalRuntimeTransport->running()) {
        QString startError;
        if (!m_externalRuntimeTransport->start(m_externalRuntimeProgram, m_externalRuntimeArguments, trustMode(), &startError)) {
            m_externalRuntimeError = startError;
            if (error) *error = startError;
            return false;
        }
    }
    if (!m_externalRuntimeTransport->waitUntilReady(3000, &readyError)) {
        m_externalRuntimeError = readyError;
        if (error) *error = readyError;
        return false;
    }

    m_externalRuntimeError.clear();
    return true;
}

bool VibeCutAgent::handoffPendingPlanToExternalRuntime(QString *error)
{
    if (error) error->clear();
    if (!externalRuntimeRequested()) return false;
    if (!m_planRuntime || !m_planRuntime->hasPendingPlan() || m_planRuntime->executing()) {
        if (error) *error = QStringLiteral("There is no non-executing pending plan to hand off.");
        return false;
    }
    if (!ensureExternalRuntimeReady(error)) return false;

    const QJsonObject plan = m_planRuntime->pendingPlanJson();
    const QString planId = plan.value(QStringLiteral("id")).toString().trimmed();
    if (planId.isEmpty()) {
        if (error) *error = QStringLiteral("Pending plan has no stable id.");
        return false;
    }
    QString handoffError;
    if (!m_externalRuntimeTransport->handoffPlan(plan, &handoffError)) {
        m_externalRuntimeError = handoffError;
        if (error) *error = handoffError;
        return false;
    }
    m_externalPlanId = planId;
    m_externalPlanExecuting = false;
    m_externalRuntimeError.clear();
    Q_EMIT planProgress(QStringLiteral("Plan %1 handed to the out-of-process runtime.").arg(planId));
    return true;
}

void VibeCutAgent::approvePendingPlanInternal(bool humanDecisionPresent, bool humanApproved)
{
    if (!m_planRuntime || !m_planRuntime->hasPendingPlan() || m_planRuntime->executing()) {
        Q_EMIT errorOccurred(QStringLiteral("There is no plan awaiting approval."));
        return;
    }
    if (m_reply) {
        Q_EMIT errorOccurred(QStringLiteral("The model request is still finishing; approval cannot start yet."));
        return;
    }

    if (externalRuntimeRequested()) {
        QString error;
        if (m_externalPlanId != m_planRuntime->pendingPlanId() && !handoffPendingPlanToExternalRuntime(&error)) {
            Q_EMIT errorOccurred(QStringLiteral("External runtime handoff failed; in-process execution was not used: %1").arg(error));
            return;
        }
        if (!ensureExternalRuntimeReady(&error)) {
            Q_EMIT errorOccurred(QStringLiteral("External runtime is unavailable; in-process execution was not used: %1").arg(error));
            return;
        }
        if (!m_externalRuntimeTransport->sendAuthorization(trustMode(), humanApproved, humanDecisionPresent, &error)) {
            Q_EMIT errorOccurred(QStringLiteral("External runtime authorization failed: %1").arg(error));
            return;
        }
        if (humanApproved || !humanDecisionPresent) {
            m_externalPlanExecuting = true;
            Q_EMIT planProgress(QStringLiteral("External runtime authorized for plan %1.").arg(m_externalPlanId));
            Q_EMIT statusChanged(QStringLiteral("Executing approved plan in external runtime…"));
        }
        return;
    }

    const QJsonObject result = m_planRuntime->approvePendingPlan();
    if (!result.value(QStringLiteral("ok")).toBool()) {
        Q_EMIT errorOccurred(result.value(QStringLiteral("error")).toString(QStringLiteral("Plan approval failed.")));
    }
}

void VibeCutAgent::resolveHostedPlan(const QString &planId, bool success,
                                    const QString &summary, const QJsonObject &result)
{
    m_externalPlanExecuting = false;
    if (planId == m_externalPlanId) m_externalPlanId.clear();
    if (!m_planRuntime) return;

    const QJsonObject resolved = m_planRuntime->resolvePendingPlanExternally(planId, success, summary, result);
    if (!resolved.value(QStringLiteral("ok")).toBool(false)) {
        const QString message = resolved.value(QStringLiteral("error")).toString(QStringLiteral("Could not reconcile external runtime completion."));
        m_externalRuntimeError = message;
        Q_EMIT errorOccurred(message);
    }
}
