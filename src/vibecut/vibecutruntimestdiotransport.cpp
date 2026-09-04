/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutruntimestdiotransport.h"

#include "vibecutruntimeprotocoladapter.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QUuid>

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
        const QJsonObject response = m_adapter->handleRequest(document.object());
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
    if (!m_adapter || !m_adapter->hasPendingPlan()) return;
    QJsonObject payload{{QStringLiteral("plan_id"), m_adapter->pendingPlanId()},
                        {QStringLiteral("reason"), reason.trimmed().isEmpty() ? QStringLiteral("Runtime disconnected.") : reason.trimmed()}};
    if (m_adapter->hasAuthorization()) payload.insert(QStringLiteral("authorization_id"), m_adapter->authorizationId());
    const QJsonObject request{{QStringLiteral("v"), 1},
                              {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                              {QStringLiteral("kind"), QStringLiteral("request")},
                              {QStringLiteral("type"), QStringLiteral("abort_plan")},
                              {QStringLiteral("payload"), payload}};
    m_adapter->handleRequest(request);
}

void VibeCutRuntimeStdioTransport::failProtocol(const QString &message)
{
    Q_EMIT diagnostic(message);
    stop(QStringLiteral("Protocol violation: %1").arg(message));
}
