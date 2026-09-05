/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include "vibecutcontracts.h"
#include "vibecutruntimecheckpoint.h"
#include "vibecutruntimeprotocoladapter.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

class QProcess;

/** NDJSON transport to an out-of-process runtime. The transport has no editor
 * authority of its own; every inbound request passes through
 * VibeCutRuntimeProtocolAdapter. Adapter-side Undo checkpoint ownership remains
 * GPL/Kdenlive-side and mirrors the integrated VibeCutPlanRuntime semantics. */
class VibeCutRuntimeStdioTransport : public QObject
{
    Q_OBJECT
public:
    static constexpr int MaxProtocolLineBytes = 2 * 1024 * 1024;

    explicit VibeCutRuntimeStdioTransport(VibeCutRuntimeProtocolAdapter *adapter, QObject *parent = nullptr);
    ~VibeCutRuntimeStdioTransport() override;

    bool start(const QString &program, const QStringList &arguments = QStringList(),
               VibeCutTrustMode helloMode = VibeCutTrustMode::Off,
               QString *error = nullptr);
    /** Wait until QProcess is actually Running. This closes the Starting->handoff
     * race without making the whole protocol synchronous. */
    bool waitUntilReady(int timeoutMs = 3000, QString *error = nullptr);
    void stop(const QString &reason = QStringLiteral("Runtime transport stopped by adapter."));
    bool running() const;

    /** Validate/store an exact GPL-host-created EditPlan in the adapter and
     * hand that same immutable plan to the proprietary child for orchestration.
     * This performs no mutation and never lets the child replace tool/input. */
    bool handoffPlan(const QJsonObject &plan, QString *error = nullptr);

    /** Send a human/adapter authorization decision to the connected runtime. */
    bool sendAuthorization(VibeCutTrustMode mode, bool humanApproved,
                           bool humanDecisionPresent = true,
                           QString *error = nullptr);

Q_SIGNALS:
    void diagnostic(const QString &message);
    void stopped(int exitCode, int exitStatus);
    /** Emitted after the child completes/aborts the staged plan and adapter-side
     * checkpoint handling has finished. */
    void hostedPlanFinished(const QString &planId, bool success, const QString &summary, const QJsonObject &result);

private Q_SLOTS:
    void readRuntimeStdout();
    void readRuntimeStderr();
    void runtimeFinished(int exitCode, int exitStatus);

private:
    bool writeEnvelope(const QJsonObject &envelope, QString *error = nullptr);
    QJsonObject dispatchRequest(const QJsonObject &request);
    QJsonObject transportError(const QJsonObject &request, const QString &code, const QString &message) const;
    void invalidatePlanForDisconnect(const QString &reason);
    void failProtocol(const QString &message);

    VibeCutRuntimeProtocolAdapter *m_adapter = nullptr;
    QProcess *m_process = nullptr;
    QByteArray m_stdoutBuffer;
    VibeCutTrustMode m_helloMode = VibeCutTrustMode::Off;
    VibeCutRuntimeCheckpoint m_checkpoint;
    bool m_stopping = false;
};
