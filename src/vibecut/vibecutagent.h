/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once

#include "sseparser.h"
#include "vibecutcontracts.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class VibeCutHooks;
class VibeCutModelProvider;
class VibeCutPlanRuntime;
class VibeCutRuntimeProtocolAdapter;
class VibeCutRuntimeStdioTransport;
class VibeCutTools;
class VibeCutToolSurface;

/** Drives one conversation with the selected model provider and governed edit runtime. */
class VibeCutAgent : public QObject
{
    Q_OBJECT
public:
    explicit VibeCutAgent(VibeCutTools *tools, QObject *parent = nullptr);
    ~VibeCutAgent() override;

    bool hasApiKey() const; // compatibility name: true when the selected provider is configured
    bool busy() const;
    bool hasPendingPlan() const;
    QString modelProviderId() const;
    VibeCutHooks *hooks() const { return m_hooks; }
    VibeCutTrustMode trustMode() const;
    void setTrustMode(VibeCutTrustMode mode);
    bool reloadModelProvider(QString *error = nullptr);

Q_SIGNALS:
    void statusChanged(const QString &status);
    void assistantTextDelta(const QString &text);
    void assistantMessage(const QString &text);
    void toolInvoked(const QString &name, const QString &argsJson);
    void toolFailed(const QString &name, const QString &errorMessage);
    void toolCompleted(const QString &name, const QString &resultJson);
    void userQuestionRaised(const QString &question);
    void backgroundProgress(const QString &message);
    void errorOccurred(const QString &message);
    void planProposed(const QString &planId, const QString &summary);
    void planProgress(const QString &message);
    void planFinished(const QString &planId, bool success, const QString &summary);
    void trustModeChanged(VibeCutTrustMode mode);

public Q_SLOTS:
    void sendUserMessage(const QString &text);
    void resetConversation();
    void approvePendingPlan();
    void cancelPendingPlan();

private Q_SLOTS:
    void onReadyRead();
    void onFinished();

private:
    void startRequest();
    void handleEvent(const SseParser::Event &event);
    void finishTurn();
    void fail(const QString &message);
    void resetStreamState();
    QString historyDiagnostic() const;
    void publishToolHook(const QString &eventName, const QString &tool, const QJsonObject &payload = QJsonObject());

    // Optional out-of-process commercial runtime. It is activated only when
    // VIBECUT_EXTERNAL_RUNTIME_PROGRAM is explicitly configured. If configured,
    // execution fails closed rather than silently falling back in-process.
    void initializeExternalRuntime();
    bool ensureExternalRuntimeReady(QString *error = nullptr);
    bool handoffPendingPlanToExternalRuntime(QString *error = nullptr);
    void approvePendingPlanInternal(bool humanDecisionPresent, bool humanApproved);
    void resolveHostedPlan(const QString &planId, bool success, const QString &summary, const QJsonObject &result);
    bool externalRuntimeRequested() const { return !m_externalRuntimeProgram.isEmpty(); }
    bool externalPlanExecuting() const { return m_externalPlanExecuting; }

    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply = nullptr;
    VibeCutTools *m_tools;
    VibeCutToolSurface *m_toolSurface = nullptr;
    VibeCutPlanRuntime *m_planRuntime = nullptr;
    VibeCutHooks *m_hooks = nullptr;
    std::unique_ptr<VibeCutModelProvider> m_provider;
    QString m_providerError;
    SseParser m_sse;

    VibeCutRuntimeProtocolAdapter *m_externalProtocolAdapter = nullptr;
    VibeCutRuntimeStdioTransport *m_externalRuntimeTransport = nullptr;
    QString m_externalRuntimeProgram;
    QStringList m_externalRuntimeArguments;
    QString m_externalRuntimeError;
    QString m_externalPlanId;
    bool m_externalPlanExecuting = false;

    QString m_systemPrompt;
    QString m_currentUserRequest;
    QJsonArray m_messages;

    QJsonArray m_blocks;
    QJsonObject m_curBlock;
    QString m_curText;
    QString m_curThinking;
    QString m_curJson;
    QString m_stopReason;
    bool m_turnFinished = false;
    bool m_autoApprovePending = false;
    int m_toolTurns = 0;
    bool m_anyToolCalledThisExchange = false;
    int m_emptyTurnRetries = 0;

    enum { kMaxToolTurns = 8, kMaxEmptyTurnRetries = 2, kMaxTokens = 8192 };
};
