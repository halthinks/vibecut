/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecutagent.h"
#include "vibecutconversationcontext.h"
#include "vibecutjobtools.h"
#include "vibecutplanruntime.h"
#include "vibecutplantools.h"
#include "vibecutprojectrules.h"
#include "vibecutsubtitletools.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QDebug>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
constexpr char kEndpoint[] = "https://api.anthropic.com/v1/messages";
constexpr char kApiVersion[] = "2023-06-01";
constexpr char kModel[] = "claude-sonnet-5";
constexpr int kMaxTokens = 8192;

const QString kSystemPrompt = QStringLiteral(
    "You are VibeCut, an assistant embedded in the Kdenlive video editor. You operate the live project only through "
    "the provided governed tools. Never invent clip ids, subtitle ids, effect names, job ids, or project state: inspect "
    "first. Read-only tools may run immediately. BEFORE any tool that edits the project, starts installation/publishing, "
    "or causes another side effect, call edit_plan_propose with the COMPLETE intended operation list and dependencies, "
    "then STOP and wait for explicit user approval. Never execute a planned mutation yourself after proposing it; the "
    "deterministic plan runtime does that. For compound requests, include every requested mutation in one plan. When a "
    "tool fails, report exactly what failed instead of guessing, and never claim success unless the tool/runtime result "
    "confirms it. For speech-to-text, inspect speech_status first; if setup is needed, include speech_setup in the plan. "
    "For subtitles, prefer the selected clip; never assume a multi-clip whole-project scope unless the user explicitly "
    "asks for it. Use ask_user only when the answer changes what should be done. Never end silently. Keep replies short.");

QByteArray compact(const QJsonObject &obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

bool isToolUse(const QJsonObject &block)
{
    return block.value(QStringLiteral("type")).toString() == QLatin1String("tool_use");
}
} // namespace

QString VibeCutAgent::historyDiagnostic() const
{
    return QStringLiteral("messages=%1 approx_bytes=%2")
        .arg(m_messages.size())
        .arg(QJsonDocument(m_messages).toJson(QJsonDocument::Compact).size());
}

VibeCutAgent::VibeCutAgent(VibeCutTools *tools, QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_tools(tools)
    , m_toolSurface(new VibeCutToolSurface(tools))
    , m_planRuntime(new VibeCutPlanRuntime(m_toolSurface, this))
    , m_model(QString::fromLatin1(kModel))
    , m_systemPrompt(kSystemPrompt)
{
    m_apiKey = qEnvironmentVariable("ANTHROPIC_API_KEY").trimmed();
    connect(m_tools, &VibeCutTools::userQuestionRaised, this, &VibeCutAgent::userQuestionRaised);
    connect(m_tools, &VibeCutTools::backgroundProgress, this, &VibeCutAgent::backgroundProgress);

    QString extensionError;
    if (!registerVibeCutSubtitleTools(*m_toolSurface, &extensionError)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] subtitle tools unavailable: %1").arg(extensionError);
    }
    extensionError.clear();
    if (!registerVibeCutJobTools(*m_toolSurface, &extensionError)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] job tools unavailable: %1").arg(extensionError);
    }
    extensionError.clear();
    if (!registerVibeCutPlanTools(*m_toolSurface, m_planRuntime, &extensionError)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] plan tool unavailable: %1").arg(extensionError);
    }

    connect(m_planRuntime, &VibeCutPlanRuntime::planProposed, this, [this](const QString &planId, const QString &summary) {
        Q_EMIT planProposed(planId, summary);
        Q_EMIT statusChanged(QStringLiteral("Awaiting plan approval"));
    });
    connect(m_planRuntime, &VibeCutPlanRuntime::planProgress, this, [this](const QString &message) {
        Q_EMIT planProgress(message);
        Q_EMIT statusChanged(QStringLiteral("Executing approved plan…"));
    });
    connect(m_planRuntime, &VibeCutPlanRuntime::planFinished, this,
            [this](const QString &planId, bool success, const QString &summary, const QJsonArray &) {
                m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                              {QStringLiteral("content"),
                                               QStringLiteral("VibeCut execution event for plan %1: %2 — %3")
                                                   .arg(planId, success ? QStringLiteral("SUCCESS") : QStringLiteral("STOPPED"), summary)}});
                Q_EMIT planFinished(planId, success, summary);
                Q_EMIT statusChanged(QStringLiteral("Ready"));
            });
}

VibeCutAgent::~VibeCutAgent()
{
    delete m_planRuntime;
    delete m_toolSurface;
}

bool VibeCutAgent::hasApiKey() const
{
    return !m_apiKey.isEmpty();
}

bool VibeCutAgent::busy() const
{
    return m_reply != nullptr || (m_planRuntime && m_planRuntime->executing());
}

bool VibeCutAgent::hasPendingPlan() const
{
    return m_planRuntime && m_planRuntime->hasPendingPlan();
}

void VibeCutAgent::sendUserMessage(const QString &text)
{
    if (m_planRuntime && (m_planRuntime->hasPendingPlan() || m_planRuntime->executing())) {
        Q_EMIT errorOccurred(QStringLiteral("Review or finish the current VibeCut plan before starting another request."));
        return;
    }
    if (m_reply) {
        Q_EMIT errorOccurred(QStringLiteral("Still working on the previous message."));
        return;
    }
    if (!hasApiKey()) {
        Q_EMIT errorOccurred(QStringLiteral("ANTHROPIC_API_KEY is not set in the environment."));
        return;
    }

    m_currentUserRequest = text;
    m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), text}});
    m_toolTurns = 0;
    m_anyToolCalledThisExchange = false;
    m_emptyTurnRetries = 0;
    startRequest();
}

void VibeCutAgent::resetConversation()
{
    if (m_planRuntime && m_planRuntime->executing()) {
        Q_EMIT errorOccurred(QStringLiteral("An approved plan is still executing; wait for its current checkpoint to finish."));
        return;
    }
    if (m_planRuntime && m_planRuntime->hasPendingPlan()) {
        m_planRuntime->cancelPendingPlan();
    }
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    m_messages = QJsonArray();
    m_currentUserRequest.clear();
    m_toolTurns = 0;
    m_anyToolCalledThisExchange = false;
    m_emptyTurnRetries = 0;
    Q_EMIT statusChanged(QStringLiteral("Ready"));
}

void VibeCutAgent::approvePendingPlan()
{
    if (!m_planRuntime || !m_planRuntime->hasPendingPlan() || m_planRuntime->executing()) {
        Q_EMIT errorOccurred(QStringLiteral("There is no plan awaiting approval."));
        return;
    }
    if (m_reply) {
        Q_EMIT errorOccurred(QStringLiteral("The model request is still finishing; approval cannot start yet."));
        return;
    }
    const QJsonObject result = m_planRuntime->approvePendingPlan();
    if (!result.value(QStringLiteral("ok")).toBool()) {
        Q_EMIT errorOccurred(result.value(QStringLiteral("error")).toString(QStringLiteral("Plan approval failed.")));
    }
}

void VibeCutAgent::cancelPendingPlan()
{
    if (!m_planRuntime) {
        return;
    }
    const QJsonObject result = m_planRuntime->cancelPendingPlan();
    if (!result.value(QStringLiteral("ok")).toBool()) {
        Q_EMIT errorOccurred(result.value(QStringLiteral("error")).toString(QStringLiteral("Plan cancellation failed.")));
    }
}

void VibeCutAgent::resetStreamState()
{
    m_sse.reset();
    m_blocks = QJsonArray();
    m_curBlock = QJsonObject();
    m_curText.clear();
    m_curThinking.clear();
    m_curJson.clear();
    m_stopReason.clear();
    m_turnFinished = false;
}

void VibeCutAgent::startRequest()
{
    if (hasPendingPlan() || (m_planRuntime && m_planRuntime->executing())) {
        return;
    }
    resetStreamState();

    const int previousMessageCount = m_messages.size();
    const int previousBytes = VibeCutConversationContext::approximateBytes(m_messages);
    m_messages = VibeCutConversationContext::compact(m_messages);
    if (m_messages.size() != previousMessageCount) {
        qInfo().noquote() << QStringLiteral("[VibeCut] compacted conversation history messages=%1->%2 bytes=%3->%4")
                                 .arg(previousMessageCount)
                                 .arg(m_messages.size())
                                 .arg(previousBytes)
                                 .arg(VibeCutConversationContext::approximateBytes(m_messages));
    }

    QString rulesError;
    const QString projectRules = VibeCutProjectRules::loadCurrentProject(&rulesError);
    if (!rulesError.isEmpty()) {
        qWarning().noquote() << QStringLiteral("[VibeCut] project rules ignored: %1").arg(rulesError);
    }
    const QString effectiveSystemPrompt = VibeCutProjectRules::appendToSystemPrompt(m_systemPrompt, projectRules);
    const QJsonObject systemBlock{{QStringLiteral("type"), QStringLiteral("text")},
                                  {QStringLiteral("text"), effectiveSystemPrompt},
                                  {QStringLiteral("cache_control"), QJsonObject{{QStringLiteral("type"), QStringLiteral("ephemeral")}}}};

    const QJsonObject body{
        {QStringLiteral("model"), m_model},
        {QStringLiteral("max_tokens"), kMaxTokens},
        {QStringLiteral("stream"), true},
        {QStringLiteral("thinking"), QJsonObject{{QStringLiteral("type"), QStringLiteral("adaptive")}}},
        {QStringLiteral("system"), QJsonArray{systemBlock}},
        {QStringLiteral("tools"), m_toolSurface->schemas()},
        {QStringLiteral("messages"), m_messages},
    };

    QNetworkRequest request{QUrl(QString::fromLatin1(kEndpoint))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    request.setRawHeader(QByteArrayLiteral("x-api-key"), m_apiKey.toUtf8());
    request.setRawHeader(QByteArrayLiteral("anthropic-version"), QByteArrayLiteral(kApiVersion));

    m_reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &VibeCutAgent::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &VibeCutAgent::onFinished);
    Q_EMIT statusChanged(QStringLiteral("Thinking…"));
}

void VibeCutAgent::onReadyRead()
{
    if (!m_reply) {
        return;
    }
    const QList<SseParser::Event> events = m_sse.feed(m_reply->readAll());
    for (const SseParser::Event &event : events) {
        handleEvent(event);
    }
}

void VibeCutAgent::handleEvent(const SseParser::Event &event)
{
    const QJsonObject object = QJsonDocument::fromJson(event.data).object();
    const QString type = object.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("content_block_start")) {
        m_curBlock = object.value(QStringLiteral("content_block")).toObject();
        m_curText.clear();
        m_curThinking.clear();
        m_curJson.clear();
    } else if (type == QLatin1String("content_block_delta")) {
        const QJsonObject delta = object.value(QStringLiteral("delta")).toObject();
        const QString deltaType = delta.value(QStringLiteral("type")).toString();
        if (deltaType == QLatin1String("text_delta")) {
            const QString text = delta.value(QStringLiteral("text")).toString();
            m_curText += text;
            Q_EMIT assistantTextDelta(text);
        } else if (deltaType == QLatin1String("thinking_delta")) {
            m_curThinking += delta.value(QStringLiteral("thinking")).toString();
        } else if (deltaType == QLatin1String("input_json_delta")) {
            m_curJson += delta.value(QStringLiteral("partial_json")).toString();
        } else if (deltaType == QLatin1String("signature_delta")) {
            m_curBlock.insert(QStringLiteral("signature"), delta.value(QStringLiteral("signature")));
        }
    } else if (type == QLatin1String("content_block_stop")) {
        const QString blockType = m_curBlock.value(QStringLiteral("type")).toString();
        if (blockType == QLatin1String("text")) {
            m_curBlock.insert(QStringLiteral("text"), m_curText);
        } else if (blockType == QLatin1String("thinking")) {
            m_curBlock.insert(QStringLiteral("thinking"), m_curThinking);
        } else if (blockType == QLatin1String("tool_use")) {
            m_curBlock.insert(QStringLiteral("input"), QJsonDocument::fromJson(m_curJson.toUtf8()).object());
        }
        m_blocks.append(m_curBlock);
        m_curBlock = QJsonObject();
    } else if (type == QLatin1String("message_delta")) {
        const QString stop = object.value(QStringLiteral("delta")).toObject().value(QStringLiteral("stop_reason")).toString();
        if (!stop.isEmpty()) {
            m_stopReason = stop;
        }
    } else if (type == QLatin1String("message_stop")) {
        finishTurn();
    } else if (type == QLatin1String("error")) {
        fail(object.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString(QStringLiteral("stream error")));
    }
}

void VibeCutAgent::onFinished()
{
    if (!m_reply) {
        return;
    }
    const QNetworkReply::NetworkError networkError = m_reply->error();
    const QString networkErrorString = m_reply->errorString();
    const QByteArray trailing = m_reply->readAll();
    if (!trailing.isEmpty()) {
        for (const SseParser::Event &event : m_sse.feed(trailing)) {
            handleEvent(event);
        }
    }

    m_reply->deleteLater();
    m_reply = nullptr;

    if (networkError != QNetworkReply::NoError && !m_turnFinished) {
        QString detail = networkErrorString;
        const QJsonObject errorObject = QJsonDocument::fromJson(trailing).object();
        const QString apiMessage = errorObject.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
        if (!apiMessage.isEmpty()) {
            detail = apiMessage;
        }
        fail(QStringLiteral("Request failed: %1").arg(detail));
        return;
    }
    if (!m_turnFinished) {
        finishTurn();
    }
}

void VibeCutAgent::finishTurn()
{
    if (m_turnFinished) {
        return;
    }
    m_turnFinished = true;
    const bool normalStop = m_stopReason == QLatin1String("end_turn") || m_stopReason.isEmpty();

    if (m_blocks.isEmpty() && normalStop && m_emptyTurnRetries < kMaxEmptyTurnRetries) {
        ++m_emptyTurnRetries;
        qWarning().noquote() << QStringLiteral("[VibeCut] empty end_turn - retrying (%1/%2) (%3)")
                                     .arg(m_emptyTurnRetries)
                                     .arg(static_cast<int>(kMaxEmptyTurnRetries))
                                     .arg(historyDiagnostic());
        Q_EMIT statusChanged(QStringLiteral("Retrying (no response)…"));
        startRequest();
        return;
    }

    if (!m_blocks.isEmpty()) {
        m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), m_blocks}});
    }

    if (m_stopReason == QLatin1String("tool_use")) {
        if (++m_toolTurns > kMaxToolTurns) {
            fail(QStringLiteral("Stopped after %1 tool turns.").arg(kMaxToolTurns));
            return;
        }

        const QHash<QString, VibeCutToolPolicy> policies = m_toolSurface->policies();
        QJsonArray directMutationBlocks;
        bool hasExplicitPlanProposal = false;
        for (const QJsonValue &value : m_blocks) {
            const QJsonObject block = value.toObject();
            if (!isToolUse(block)) {
                continue;
            }
            const QString name = block.value(QStringLiteral("name")).toString();
            if (name == QLatin1String("edit_plan_propose")) {
                hasExplicitPlanProposal = true;
            }
            const auto policy = policies.constFind(name);
            if (policy == policies.constEnd() || policy.value().risk != VibeCutToolRisk::ReadOnly) {
                directMutationBlocks.append(block);
            }
        }

        QJsonObject directProposal;
        if (!directMutationBlocks.isEmpty() && !hasExplicitPlanProposal && m_planRuntime && !m_planRuntime->hasPendingPlan()) {
            directProposal = m_planRuntime->proposeDirectToolCalls(directMutationBlocks, m_currentUserRequest);
        }

        QJsonArray toolResults;
        for (const QJsonValue &value : m_blocks) {
            const QJsonObject block = value.toObject();
            if (!isToolUse(block)) {
                continue;
            }
            const QString name = block.value(QStringLiteral("name")).toString();
            const QString id = block.value(QStringLiteral("id")).toString();
            const QJsonObject input = block.value(QStringLiteral("input")).toObject();
            const auto policy = policies.constFind(name);
            const bool mutation = policy == policies.constEnd() || policy.value().risk != VibeCutToolRisk::ReadOnly;

            QJsonObject result;
            if (mutation) {
                if (m_planRuntime && m_planRuntime->hasPendingPlan()) {
                    result = QJsonObject{{QStringLiteral("ok"), false},
                                         {QStringLiteral("not_executed"), true},
                                         {QStringLiteral("requires_approval"), true},
                                         {QStringLiteral("plan_id"), m_planRuntime->pendingPlanId()},
                                         {QStringLiteral("error"), QStringLiteral("Not executed: the requested change is awaiting user plan approval.")}};
                } else if (directProposal.value(QStringLiteral("ok")).toBool()) {
                    result = QJsonObject{{QStringLiteral("ok"), false},
                                         {QStringLiteral("not_executed"), true},
                                         {QStringLiteral("requires_approval"), true},
                                         {QStringLiteral("plan_id"), m_planRuntime ? m_planRuntime->pendingPlanId() : QString()},
                                         {QStringLiteral("error"), QStringLiteral("Not executed: the requested change is awaiting user plan approval.")}};
                } else if (hasExplicitPlanProposal) {
                    result = QJsonObject{{QStringLiteral("ok"), false},
                                         {QStringLiteral("not_executed"), true},
                                         {QStringLiteral("requires_approval"), true},
                                         {QStringLiteral("error"),
                                          QStringLiteral("Not executed: direct project changes are blocked while this turn's edit_plan_propose is reviewed.")}};
                } else {
                    result = directProposal.isEmpty()
                                 ? QJsonObject{{QStringLiteral("ok"), false},
                                               {QStringLiteral("not_executed"), true},
                                               {QStringLiteral("error"), QStringLiteral("Ungoverned project-changing tool call was blocked.")}}
                                 : directProposal;
                }
            } else {
                m_anyToolCalledThisExchange = true;
                Q_EMIT toolInvoked(name, QString::fromUtf8(compact(input)));
                result = m_toolSurface->invoke(name, input);
                Q_EMIT toolCompleted(name, QString::fromUtf8(compact(result)));
                if (!result.value(QStringLiteral("ok")).toBool()) {
                    Q_EMIT toolFailed(name, result.value(QStringLiteral("error")).toString(QStringLiteral("unknown error")));
                }
            }

            toolResults.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_result")},
                                           {QStringLiteral("tool_use_id"), id},
                                           {QStringLiteral("content"), QString::fromUtf8(compact(result))}});
        }
        m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), toolResults}});

        if (m_planRuntime && m_planRuntime->hasPendingPlan()) {
            Q_EMIT statusChanged(QStringLiteral("Awaiting plan approval"));
            return;
        }
        startRequest();
        return;
    }

    QString finalText;
    for (const QJsonValue &value : m_blocks) {
        const QJsonObject block = value.toObject();
        if (block.value(QStringLiteral("type")).toString() == QLatin1String("text")) {
            finalText += block.value(QStringLiteral("text")).toString();
        }
    }
    finalText = finalText.trimmed();

    if (m_stopReason != QLatin1String("end_turn") && !m_stopReason.isEmpty()) {
        qWarning().noquote() << QStringLiteral("[VibeCut] turn ended with stop_reason=%1 (%2)").arg(m_stopReason, historyDiagnostic());
        fail(QStringLiteral("Turn ended unexpectedly (%1) instead of finishing normally.").arg(m_stopReason));
        return;
    }
    if (finalText.isEmpty() && !m_anyToolCalledThisExchange) {
        fail(QStringLiteral("The model didn't respond or take any action. Try again or rephrase."));
        return;
    }

    Q_EMIT assistantMessage(finalText);
    Q_EMIT statusChanged(QStringLiteral("Ready"));
}

void VibeCutAgent::fail(const QString &message)
{
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    Q_EMIT errorOccurred(message);
    Q_EMIT statusChanged(hasPendingPlan() ? QStringLiteral("Awaiting plan approval") : QStringLiteral("Ready"));
}
