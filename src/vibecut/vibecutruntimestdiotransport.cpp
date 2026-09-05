/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutruntimestdiotransport.h"

#include "vibecutruntimeprotocoladapter.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QUuid>

namespace {
bool protocolSuccess(const QJsonObject &response)
{
    return response.value(QStringLiteral("type")).toString() != QLatin1String("error") &&
           response.value(QStringLiteral("kind")).toString() == QLatin1String("response") &&
           response.value(QStringLiteral("payload")).toObject().value(QStringLiteral("ok")).toBool(false);
}
}

VibeCutRuntimeStdioTransport::VibeCutRuntimeStdioTransport(VibeCutRuntimeProtocolAdapter *adapter, QObject *parent)
    : QObject(parent)
    , m_adapter(adapter)
    , m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &VibeCutRuntimeStdioTransport::readRuntimeStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &VibeCutRuntimeStdioTransport::readRuntimeStderr);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) { runtimeFinished(code, static_cast<int>(status)); });
    if (m_adapter) {
        connect(m_adapter, &VibeCutRuntimeProtocolAdapter::outboundEnvelope, this,
                [this](const QJsonObject &envelope) {
            if (envelope.value(QStringLiteral("type")).toString() == QLatin1String("job_update")) {
                const QString jobId = envelope.value(QStringLiteral("payload")).toObject()
                                          .value(QStringLiteral("job")).toObject()
                                          .value(QStringLiteral("id")).toString().trimmed();
                if (!m_adapter || !m_adapter->ownsProtocolJob(jobId)) return;
            }
            QString error;
            if (!writeEnvelope(envelope, &error) && !error.isEmpty()) Q_EMIT diagnostic(error);
        });
    }
}

VibeCutRuntimeStdioTransport::~VibeCutRuntimeStdioTransport()
{
    if (running()) stop(QStringLiteral("Runtime transport destroyed."));
}

bool VibeCutRuntimeStdioTransport::start(const QString &program, const QStringList &arguments,
                                         VibeCutTrustMode helloMode, QString *error)
{
    if (error) error->clear();
    if (!m_adapter) {
        if (error) *error = QStringLiteral("Runtime stdio transport requires a protocol adapter.");
        return false;
    }
    if (running()) {
        if (error) *error = QStringLiteral("Runtime stdio transport is already running.");
        return false;
    }
    const QString executable = program.trimmed();
    if (executable.isEmpty()) {
        if (error) *error = QStringLiteral("Runtime program must not be empty.");
        return false;
    }

    if (m_checkpoint.macroOpen()) {
        QString rollbackError;
        if (!m_checkpoint.rollbackOpen(&rollbackError)) {
            if (error) *error = rollbackError;
            return false;
        }
    }
    m_checkpoint.reset();
    m_stdoutBuffer.clear();
    m_helloMode = helloMode;
    m_stopping = false;
    connect(m_process, &QProcess::started, this, [this]() {
        QString writeError;
        const QJsonObject hello = m_adapter->helloEnvelope(QUuid::createUuid().toString(QUuid::WithoutBraces), m_helloMode);
        if (!writeEnvelope(hello, &writeError)) failProtocol(writeError);
    }, Qt::SingleShotConnection);
    m_process->start(executable, arguments);
    return true;
}

void VibeCutRuntimeStdioTransport::stop(const QString &reason)
{
    if (!m_process) return;
    invalidatePlanForDisconnect(reason);
    m_stopping = true;
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(1000)) m_process->kill();
    }
}

bool VibeCutRuntimeStdioTransport::running() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool VibeCutRuntimeStdioTransport::sendAuthorization(VibeCutTrustMode mode, bool humanApproved,
                                                     bool humanDecisionPresent, QString *error)
{
    if (error) error->clear();
    if (!running()) {
        if (error) *error = QStringLiteral("Runtime process is not connected.");
        return false;
    }
    if (!m_adapter) {
        if (error) *error = QStringLiteral("Protocol adapter is unavailable.");
        return false;
    }
    const QJsonObject envelope = m_adapter->authorizePending(mode, humanApproved, humanDecisionPresent);
    return writeEnvelope(envelope, error);
}

bool VibeCutRuntimeStdioTransport::writeEnvelope(const QJsonObject &envelope, QString *error)
{
    if (error) error->clear();
    if (!running()) {
        if (error) *error = QStringLiteral("Cannot write runtime protocol message because process is not running.");
        return false;
    }
    QByteArray data = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    if (data.isEmpty() || data.size() > MaxProtocolLineBytes) {
        if (error) *error = QStringLiteral("Outbound runtime protocol message exceeds the bounded NDJSON line contract.");
        return false;
    }
    data.append('\n');
    if (m_process->write(data) != data.size()) {
        if (error) *error = QStringLiteral("Could not write complete runtime protocol envelope to child stdin.");
        return false;
    }
    return true;
}

QJsonObject VibeCutRuntimeStdioTransport::transportError(const QJsonObject &request,
                                                         const QString &code,
                                                         const QString &message) const
{
    return QJsonObject{{QStringLiteral("v"), 1},
                       {QStringLiteral("id"), request.value(QStringLiteral("id")).toString()},
                       {QStringLiteral("kind"), QStringLiteral("response")},
                       {QStringLiteral("type"), QStringLiteral("error")},
                       {QStringLiteral("payload"), QJsonObject{{QStringLiteral("code"), code},
                                                               {QStringLiteral("message"), message},
                                                               {QStringLiteral("retryable"), false},
                                                               {QStringLiteral("details"), QJsonObject()}}}};
}

QJsonObject VibeCutRuntimeStdioTransport::dispatchRequest(const QJsonObject &request)
{
    if (!m_adapter) return transportError(request, QStringLiteral("adapter_unavailable"), QStringLiteral("Protocol adapter is unavailable."));
    const QString type = request.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("invoke")) {
        const QString operationId = request.value(QStringLiteral("payload")).toObject()
                                        .value(QStringLiteral("operation_id")).toString().trimmed();
        VibeCutToolPolicy policy;
        QString policyError;
        const bool hasApprovedPolicy = m_adapter->approvedOperationPolicy(operationId, policy, &policyError);
        if (hasApprovedPolicy && policy.mutatesProject) {
            QString beginError;
            if (!m_checkpoint.beginForMutation(m_adapter->activePlanObjective(), &beginError)) {
                return transportError(request, QStringLiteral("checkpoint_begin_failed"), beginError);
            }
        }

        QJsonObject response = m_adapter->handleRequest(request);
        if (response.value(QStringLiteral("type")).toString() == QLatin1String("error")) {
            if (m_checkpoint.macroOpen()) {
                QString commitError;
                if (!m_checkpoint.commitForCompletion(&commitError)) {
                    return transportError(request, QStringLiteral("checkpoint_commit_failed"), commitError);
                }
                m_adapter->synchronizeExpectedRevision();
            }
            return response;
        }

        QJsonObject payload = response.value(QStringLiteral("payload")).toObject();
        if (!payload.value(QStringLiteral("ok")).toBool(false)) {
            const bool wasOpen = m_checkpoint.macroOpen();
            QString rollbackError;
            if (!m_checkpoint.rollbackOpen(&rollbackError)) {
                return transportError(request, QStringLiteral("checkpoint_rollback_failed"), rollbackError);
            }
            payload.insert(QStringLiteral("checkpoint_rolled_back"), wasOpen);
            payload.insert(QStringLiteral("checkpoint_rollback_scope"), QStringLiteral("current_open_synchronous_macro"));
            payload.insert(QStringLiteral("revision_after_rollback"), static_cast<qint64>(m_adapter->protocolProjectRevision()));
            response.insert(QStringLiteral("payload"), payload);
            return response;
        }

        if (payload.value(QStringLiteral("started")).toBool(false)) {
            const bool wasOpen = m_checkpoint.macroOpen();
            QString commitError;
            if (!m_checkpoint.commitBeforeAsync(&commitError)) {
                return transportError(request, QStringLiteral("checkpoint_commit_failed"), commitError);
            }
            const quint64 revision = m_adapter->synchronizeExpectedRevision();
            payload.insert(QStringLiteral("revision_after"), static_cast<qint64>(revision));
            payload.insert(QStringLiteral("checkpoint_committed_before_async"), wasOpen);
            payload.insert(QStringLiteral("checkpoint_semantics"), QStringLiteral("mirrors_integrated_runtime_close_before_async"));
            response.insert(QStringLiteral("payload"), payload);
        }
        return response;
    }

    if (type == QLatin1String("complete_plan")) {
        QJsonObject response = m_adapter->handleRequest(request);
        if (!protocolSuccess(response)) return response;

        const bool wasOpen = m_checkpoint.macroOpen();
        QString commitError;
        if (!m_checkpoint.commitForCompletion(&commitError)) {
            QString rollbackError;
            m_checkpoint.rollbackOpen(&rollbackError);
            return transportError(request, QStringLiteral("checkpoint_commit_failed"), commitError);
        }
        QJsonObject payload = response.value(QStringLiteral("payload")).toObject();
        payload.insert(QStringLiteral("project_revision"), static_cast<qint64>(m_adapter->protocolProjectRevision()));
        payload.insert(QStringLiteral("checkpoint_committed"), wasOpen);
        payload.insert(QStringLiteral("committed_checkpoint_count"), m_checkpoint.committedCheckpointCount());
        payload.insert(QStringLiteral("rolled_back_checkpoint_count"), m_checkpoint.rolledBackCheckpointCount());
        payload.insert(QStringLiteral("checkpoint_rollback_parity"), true);
        payload.insert(QStringLiteral("checkpoint_semantics"), QStringLiteral("same_current_synchronous_checkpoint_scope_as_integrated_VibeCutPlanRuntime"));
        response.insert(QStringLiteral("payload"), payload);
        m_checkpoint.reset();
        return response;
    }

    if (type == QLatin1String("abort_plan")) {
        QJsonObject response = m_adapter->handleRequest(request);
        if (!protocolSuccess(response)) return response;

        const bool wasOpen = m_checkpoint.macroOpen();
        QString rollbackError;
        if (!m_checkpoint.rollbackOpen(&rollbackError)) {
            return transportError(request, QStringLiteral("checkpoint_rollback_failed"), rollbackError);
        }
        QJsonObject payload = response.value(QStringLiteral("payload")).toObject();
        payload.insert(QStringLiteral("rollback_performed"), wasOpen);
        payload.insert(QStringLiteral("rollback_scope"), QStringLiteral("current_open_synchronous_macro_only"));
        payload.insert(QStringLiteral("committed_checkpoint_count"), m_checkpoint.committedCheckpointCount());
        payload.insert(QStringLiteral("rolled_back_checkpoint_count"), m_checkpoint.rolledBackCheckpointCount());
        payload.insert(QStringLiteral("project_revision_after_abort"), static_cast<qint64>(m_adapter->protocolProjectRevision()));
        payload.insert(QStringLiteral("checkpoint_rollback_parity"), true);
        response.insert(QStringLiteral("payload"), payload);
        m_checkpoint.reset();
        return response;
    }

    return m_adapter->handleRequest(request);
}

void VibeCutRuntimeStdioTransport::readRuntimeStdout()
{
    if (!m_process || !m_adapter) return;
    m_stdoutBuffer.append(m_process->readAllStandardOutput());
    if (m_stdoutBuffer.size() > MaxProtocolLineBytes * 2 && !m_stdoutBuffer.contains('\n')) {
        failProtocol(QStringLiteral("Runtime stdout exceeded the bounded protocol buffer without a newline."));
        return;
    }

    while (true) {
        const int newline = m_stdoutBuffer.indexOf('\n');
        if (newline < 0) break;
        QByteArray line = m_stdoutBuffer.left(newline);
        m_stdoutBuffer.remove(0, newline + 1);
        if (line.endsWith('\r')) line.chop(1);
        if (line.trimmed().isEmpty()) continue;
        if (line.size() > MaxProtocolLineBytes) {
            failProtocol(QStringLiteral("Runtime emitted an oversized protocol line."));
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            failProtocol(QStringLiteral("Runtime emitted malformed protocol JSON: %1").arg(parseError.errorString()));
            return;
        }
        const QJsonObject response = dispatchRequest(document.object());
        QString writeError;
        if (!writeEnvelope(response, &writeError)) {
            failProtocol(writeError);
            return;
        }
    }

    if (m_stdoutBuffer.size() > MaxProtocolLineBytes) {
        failProtocol(QStringLiteral("Runtime emitted an oversized unterminated protocol line."));
    }
}

void VibeCutRuntimeStdioTransport::readRuntimeStderr()
{
    if (!m_process) return;
    const QString text = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
    if (!text.isEmpty()) Q_EMIT diagnostic(text.right(16384));
}

void VibeCutRuntimeStdioTransport::runtimeFinished(int exitCode, int exitStatus)
{
    if (!m_stopping) invalidatePlanForDisconnect(QStringLiteral("Runtime process exited before adapter lifecycle completion."));
    m_stdoutBuffer.clear();
    m_stopping = false;
    Q_EMIT stopped(exitCode, exitStatus);
}

void VibeCutRuntimeStdioTransport::invalidatePlanForDisconnect(const QString &reason)
{
    if (!m_adapter || !m_adapter->hasPendingPlan()) {
        if (m_checkpoint.macroOpen()) {
            QString rollbackError;
            if (!m_checkpoint.rollbackOpen(&rollbackError) && !rollbackError.isEmpty()) Q_EMIT diagnostic(rollbackError);
        }
        m_checkpoint.reset();
        return;
    }
    QJsonObject payload{{QStringLiteral("plan_id"), m_adapter->pendingPlanId()},
                        {QStringLiteral("reason"), reason.trimmed().isEmpty() ? QStringLiteral("Runtime disconnected.") : reason.trimmed()}};
    if (m_adapter->hasAuthorization()) payload.insert(QStringLiteral("authorization_id"), m_adapter->authorizationId());
    const QJsonObject request{{QStringLiteral("v"), 1},
                              {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                              {QStringLiteral("kind"), QStringLiteral("request")},
                              {QStringLiteral("type"), QStringLiteral("abort_plan")},
                              {QStringLiteral("payload"), payload}};
    const QJsonObject response = dispatchRequest(request);
    if (response.value(QStringLiteral("type")).toString() == QLatin1String("error")) {
        QString rollbackError;
        if (!m_checkpoint.rollbackOpen(&rollbackError) && !rollbackError.isEmpty()) Q_EMIT diagnostic(rollbackError);
        m_checkpoint.reset();
    }
}

void VibeCutRuntimeStdioTransport::failProtocol(const QString &message)
{
    Q_EMIT diagnostic(message);
    stop(QStringLiteral("Protocol violation: %1").arg(message));
}
