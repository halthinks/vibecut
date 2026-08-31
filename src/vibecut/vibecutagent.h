/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "sseparser.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class VibeCutPlanRuntime;
class VibeCutTools;
class VibeCutToolSurface;

/** @brief Drives one conversation with the model and governed edit runtime. */
class VibeCutAgent : public QObject
{
    Q_OBJECT
public:
    explicit VibeCutAgent(VibeCutTools *tools, QObject *parent = nullptr);
    ~VibeCutAgent() override;

    bool hasApiKey() const;
    bool busy() const;
    bool hasPendingPlan() const;

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

    /** Reviewable plan state. No project mutation has happened when
     *  planProposed fires. */
    void planProposed(const QString &planId, const QString &summary);
    void planProgress(const QString &message);
    void planFinished(const QString &planId, bool success, const QString &summary);

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
    void handleEvent(const SseParser::Event &ev);
    void finishTurn();
    void fail(const QString &message);
    void resetStreamState();
    QString historyDiagnostic() const;

    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply = nullptr;
    VibeCutTools *m_tools;
    VibeCutToolSurface *m_toolSurface = nullptr;
    VibeCutPlanRuntime *m_planRuntime = nullptr;
    SseParser m_sse;

    QString m_apiKey;
    QString m_model;
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
    int m_toolTurns = 0;

    bool m_anyToolCalledThisExchange = false;
    int m_emptyTurnRetries = 0;

    enum { kMaxToolTurns = 8, kMaxEmptyTurnRetries = 2 };
};
