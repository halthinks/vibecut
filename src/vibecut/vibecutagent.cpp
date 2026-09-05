/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecutagent.h"
#include "vibecutconversationcontext.h"
#include "vibecuthooks.h"
#include "vibecutjobmanager.h"
#include "vibecutjobtools.h"
#include "vibecutmediatools.h"
#include "vibecutmodelprovider.h"
#include "vibecutplanruntime.h"
#include "vibecutplantools.h"
#include "vibecutprojectrules.h"
#include "vibecutruntimestdiotransport.h"
#include "vibecutsubtitletools.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QDebug>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
const QString kSystemPrompt = QStringLiteral(
    "You are VibeCut, an assistant embedded in the Kdenlive video editor. You operate the live project only through "
    "the provided governed tools. Never invent clip ids, subtitle ids, effect names, job ids, or project state: inspect "
    "first. Read-only tools may run immediately. BEFORE any tool that edits the project, starts installation/publishing, "
    "or causes another side effect, call edit_plan_propose with the COMPLETE intended operation list and dependencies, "
    "then STOP and wait for approval when the active trust policy requires it. Never execute a planned mutation yourself "
    "after proposing it; the deterministic plan runtime does that. For compound requests, include every requested mutation "
    "in one plan. When a tool fails, report exactly what failed instead of guessing, and never claim success unless the "
    "tool/runtime result confirms it. For speech-to-text, inspect speech_status first; if setup is needed, include "
    "speech_setup in the plan. For subtitles, prefer the selected clip; never assume a multi-clip whole-project scope unless "
    "the user explicitly asks for it. Use media_search for time-ranged project knowledge across transcript and clips. Use "
    "ask_user only when the answer changes what should be done. Never end silently. Keep replies short.");

QByteArray compact(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool isToolUse(const QJsonObject &block)
{
    return block.value(QStringLiteral("type")).toString() == QLatin1String("tool_use");
}

QString trustName(VibeCutTrustMode mode)
{
    switch (mode) {
    case VibeCutTrustMode::Off: return QStringLiteral("review");
    case VibeCutTrustMode::Auto: return QStringLiteral("auto");
    case VibeCutTrustMode::Turbo: return QStringLiteral("turbo");
    }
    return QStringLiteral("review");
}

QString jobStateName(VibeCutJobState state)
{
    switch (state) {
    case VibeCutJobState::Queued: return QStringLiteral("queued");
    case VibeCutJobState::Running: return QStringLiteral("running");
    case VibeCutJobState::CancelRequested: return QStringLiteral("cancel_requested");
    case VibeCutJobState::Succeeded: return QStringLiteral("succeeded");
    case VibeCutJobState::Failed: return QStringLiteral("failed");
    case VibeCutJobState::Cancelled: return QStringLiteral("cancelled");
    }
    return QStringLiteral("unknown");
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
    , m_hooks(new VibeCutHooks(this))
    , m_systemPrompt(kSystemPrompt)
{
    m_provider = VibeCutModelProviderRegistry::global().createConfigured(&m_providerError);

    connect(m_tools, &VibeCutTools::userQuestionRaised, this, &VibeCutAgent::userQuestionRaised);
    connect(m_tools, &VibeCutTools::backgroundProgress, this, [this](const QString &message) {
        Q_EMIT backgroundProgress(message);
        m_hooks->publish(QStringLiteral("job.progress_message"), QJsonObject{{QStringLiteral("message"), message}});
    });

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
    extensionError.clear();
    if (!registerVibeCutMediaTools(*m_toolSurface, &extensionError)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] media tools unavailable: %1").arg(extensionError);
    }

    // Start the optional child only after all normal tool families have been
    // registered so its hello snapshot is the same governed surface the model
    // and integrated runtime see.
    initializeExternalRuntime();

    m_hooks->registerContextProvider(QStringLiteral("vibecut_runtime"), [this]() {
        return QJsonObject{{QStringLiteral("provider"), modelProviderId()},
                           {QStringLiteral("trust_mode"), trustName(trustMode())},
                           {QStringLiteral("project_revision"), static_cast<qint64>(m_toolSurface ? m_toolSurface->projectRevision() : 0)},
                           {QStringLiteral("pending_plan"), hasPendingPlan()},
                           {QStringLiteral("execution_runtime"), externalRuntimeRequested() ? QStringLiteral("external_process") : QStringLiteral("integrated_gpl")},
                           {QStringLiteral("external_runtime_running"), m_externalRuntimeTransport && m_externalRuntimeTransport->running()}};
    });

    if (m_tools && m_tools->jobManager()) {
        connect(m_tools->jobManager(), &VibeCutJobManager::jobAdded, this, [this](const QString &jobId) {
            m_hooks->publish(QStringLiteral("job.added"), QJsonObject{{QStringLiteral("job_id"), jobId}});
        });
        connect(m_tools->jobManager(), &VibeCutJobManager::jobChanged, this, [this](const QString &jobId) {
            VibeCutJob job;
            if (!m_tools->jobManager()->job(jobId, job)) return;
            m_hooks->publish(QStringLiteral("job.changed"), QJsonObject{{QStringLiteral("job_id"), job.id},
                                                                         {QStringLiteral("kind"), job.kind},
                                                                         {QStringLiteral("label"), job.label},
                                                                         {QStringLiteral("state"), jobStateName(job.state)},
                                                                         {QStringLiteral("progress"), job.progress},
                                                                         {QStringLiteral("message"), job.message}});
        });
    }

    connect(m_planRuntime, &VibeCutPlanRuntime::planProposed, this, [this](const QString &planId, const QString &summary) {
        m_hooks->publish(QStringLiteral("plan.proposed"), QJsonObject{{QStringLiteral("plan_id"), planId},
                                                                       {QStringLiteral("summary"), summary},
                                                                       {QStringLiteral("requires_confirmation"), m_planRuntime->pendingRequiresConfirmation()},
                                                                       {QStringLiteral("execution_runtime"), externalRuntimeRequested() ? QStringLiteral("external_process") : QStringLiteral("integrated_gpl")}});

        if (externalRuntimeRequested()) {
            QString handoffError;
            if (!handoffPendingPlanToExternalRuntime(&handoffError)) {
                m_externalRuntimeError = handoffError;
                Q_EMIT errorOccurred(QStringLiteral("External runtime plan handoff failed; in-process execution is disabled for this configured mode: %1").arg(handoffError));
            }
        }

        if (m_planRuntime->pendingRequiresConfirmation()) {
            Q_EMIT planProposed(planId, summary);
            Q_EMIT statusChanged(QStringLiteral("Awaiting plan approval"));
        } else {
            m_autoApprovePending = true;
            Q_EMIT planProgress(QStringLiteral("Trust policy auto-approved plan %1.").arg(planId));
            Q_EMIT statusChanged(QStringLiteral("Finishing model turn before auto-approval…"));
        }
    });
    connect(m_planRuntime, &VibeCutPlanRuntime::planApproved, this, [this](const QString &planId) {
        m_hooks->publish(QStringLiteral("plan.approved"), QJsonObject{{QStringLiteral("plan_id"), planId},
                                                                       {QStringLiteral("trust_mode"), trustName(trustMode())},
                                                                       {QStringLiteral("execution_runtime"), QStringLiteral("integrated_gpl")}});
    });
    connect(m_planRuntime, &VibeCutPlanRuntime::planProgress, this, [this](const QString &message) {
        m_hooks->publish(QStringLiteral("plan.progress"), QJsonObject{{QStringLiteral("message"), message}});
        Q_EMIT planProgress(message);
        Q_EMIT statusChanged(externalPlanExecuting() ? QStringLiteral("Executing approved plan in external runtime…")
                                                     : QStringLiteral("Executing approved plan…"));
    });
    connect(m_planRuntime, &VibeCutPlanRuntime::planFinished, this,
            [this](const QString &planId, bool success, const QString &summary, const QJsonArray &results) {
                m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                              {QStringLiteral("content"), QStringLiteral("VibeCut execution event for plan %1: %2 — %3")
                                                                               .arg(planId, success ? QStringLiteral("SUCCESS") : QStringLiteral("STOPPED"), summary)}});
                m_hooks->publish(QStringLiteral("plan.finished"), QJsonObject{{QStringLiteral("plan_id"), planId},
                                                                               {QStringLiteral("success"), success},
                                                                               {QStringLiteral("summary"), summary},
                                                                               {QStringLiteral("results"), results}});
                Q_EMIT planFinished(planId, success, summary);
                Q_EMIT statusChanged(QStringLiteral("Ready"));
            });
}

VibeCutAgent::~VibeCutAgent()
{
    // External transport may call back into the protocol adapter while stopping,
    // so tear both down before deleting their shared VibeCutToolSurface.
    if (m_externalRuntimeTransport) {
        disconnect(m_externalRuntimeTransport, nullptr, this, nullptr);
        if (m_externalRuntimeTransport->running()) m_externalRuntimeTransport->stop(QStringLiteral("VibeCut agent is shutting down."));
        delete m_externalRuntimeTransport;
        m_externalRuntimeTransport = nullptr;
    }
    if (m_externalProtocolAdapter) {
        delete m_externalProtocolAdapter;
        m_externalProtocolAdapter = nullptr;
    }
    delete m_planRuntime;
    delete m_toolSurface;
}

bool VibeCutAgent::hasApiKey() const
{
    return static_cast<bool>(m_provider);
}

bool VibeCutAgent::busy() const
{
    return m_reply != nullptr || externalPlanExecuting() || (m_planRuntime && m_planRuntime->executing());
}

bool VibeCutAgent::hasPendingPlan() const
{
    return m_planRuntime && m_planRuntime->hasPendingPlan();
}

QString VibeCutAgent::modelProviderId() const
{
    return m_provider ? m_provider->id() : QString();
}

VibeCutTrustMode VibeCutAgent::trustMode() const
{
    return m_planRuntime ? m_planRuntime->trustMode() : VibeCutTrustMode::Off;
}

void VibeCutAgent::setTrustMode(VibeCutTrustMode mode)
{
    if (!m_planRuntime || externalPlanExecuting() || m_planRuntime->hasPendingPlan() || m_planRuntime->executing()) {
        Q_EMIT errorOccurred(QStringLiteral("Trust mode cannot change while a plan is pending or executing."));
        return;
    }
    if (m_planRuntime->trustMode() == mode) return;
    m_planRuntime->setTrustMode(mode);
    m_hooks->publish(QStringLiteral("trust.changed"), QJsonObject{{QStringLiteral("mode"), trustName(mode)}});
    Q_EMIT trustModeChanged(mode);
}

void VibeCutAgent::sendUserMessage(const QString &text)
{
    if (externalPlanExecuting() || (m_planRuntime && (m_planRuntime->hasPendingPlan() || m_planRuntime->executing()))) {
        Q_EMIT errorOccurred(QStringLiteral("Review or finish the current VibeCut plan before starting another request."));
        return;
    }
    if (m_reply) {
        Q_EMIT errorOccurred(QStringLiteral("Still working on the previous message."));
        return;
    }
    if (!m_provider) {
        Q_EMIT errorOccurred(m_providerError.isEmpty() ? QStringLiteral("No VibeCut model provider is configured.") : m_providerError);
        return;
    }

    m_currentUserRequest = text;
    m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), text}});
    m_toolTurns = 0;
    m_anyToolCalledThisExchange = false;
    m_emptyTurnRetries = 0;
    m_hooks->publish(QStringLiteral("conversation.request"), QJsonObject{{QStringLiteral("text"), text}});
    startRequest();
}

void VibeCutAgent::resetConversation()
{
    if (externalPlanExecuting() || (m_planRuntime && m_planRuntime->executing())) {
        Q_EMIT errorOccurred(QStringLiteral("An approved plan is still executing; cancel or finish its current checkpoint first."));
        return;
    }
    if (m_planRuntime && m_planRuntime->hasPendingPlan()) cancelPendingPlan();
    if (m_planRuntime && m_planRuntime->hasPendingPlan()) return;
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    m_messages = QJsonArray();
    m_currentUserRequest.clear();
    m_autoApprovePending = false;
    m_toolTurns = 0;
    m_anyToolCalledThisExchange = false;
    m_emptyTurnRetries = 0;
    m_hooks->publish(QStringLiteral("conversation.reset"));
    Q_EMIT statusChanged(QStringLiteral("Ready"));
}

void VibeCutAgent::approvePendingPlan()
{
    m_autoApprovePending = false;
    approvePendingPlanInternal(true, true);
}

void VibeCutAgent::cancelPendingPlan()
{
    m_autoApprovePending = false;
    if (!m_planRuntime || !m_planRuntime->hasPendingPlan()) return;

    if (externalRuntimeRequested() && m_externalPlanId == m_planRuntime->pendingPlanId()) {
        if (m_externalPlanExecuting) {
            if (!m_externalRuntimeTransport || !m_externalRuntimeTransport->running()) {
                Q_EMIT errorOccurred(QStringLiteral("External runtime execution is active but its transport is unavailable."));
                return;
            }
            m_externalRuntimeTransport->stop(QStringLiteral("User cancelled the externally executed VibeCut plan."));
            return;
        }

        if (m_externalRuntimeTransport && m_externalRuntimeTransport->running()) {
            QString rejectionError;
            if (!m_externalRuntimeTransport->sendAuthorization(trustMode(), false, true, &rejectionError)) {
                Q_EMIT errorOccurred(QStringLiteral("Could not reject the staged external plan: %1").arg(rejectionError));
                return;
            }
        }
        m_externalPlanId.clear();
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
    if (!m_provider || hasPendingPlan() || externalPlanExecuting() || (m_planRuntime && m_planRuntime->executing())) return;
    resetStreamState();

    const int previousMessageCount = m_messages.size();
    const int previousBytes = VibeCutConversationContext::approximateBytes(m_messages);
    m_messages = VibeCutConversationContext::compact(m_messages);
    if (m_messages.size() != previousMessageCount) {
        qInfo().noquote() << QStringLiteral("[VibeCut] compacted conversation history messages=%1->%2 bytes=%3->%4")
                                 .arg(previousMessageCount).arg(m_messages.size()).arg(previousBytes)
                                 .arg(VibeCutConversationContext::approximateBytes(m_messages));
    }

    QString rulesError;
    const QString projectRules = VibeCutProjectRules::loadCurrentProject(&rulesError);
    if (!rulesError.isEmpty()) qWarning().noquote() << QStringLiteral("[VibeCut] project rules ignored: %1").arg(rulesError);
    QString effectiveSystemPrompt = VibeCutProjectRules::appendToSystemPrompt(m_systemPrompt, projectRules);
    const QJsonObject extensionContext = m_hooks->collectContext();
    if (!extensionContext.isEmpty()) {
        effectiveSystemPrompt += QStringLiteral("\n\n## Registered VibeCut extension context\n") +
                                 QString::fromUtf8(QJsonDocument(extensionContext).toJson(QJsonDocument::Compact));
    }

    const VibeCutModelRequest modelRequest = m_provider->buildRequest(effectiveSystemPrompt, m_toolSurface->schemas(), m_messages, kMaxTokens);
    if (!modelRequest.endpoint.isValid()) {
        fail(QStringLiteral("The selected VibeCut provider produced an invalid endpoint."));
        return;
    }
    QNetworkRequest request(modelRequest.endpoint);
    for (auto it = modelRequest.headers.constBegin(); it != modelRequest.headers.constEnd(); ++it) request.setRawHeader(it.key(), it.value());

    m_hooks->publish(QStringLiteral("model.request"), QJsonObject{{QStringLiteral("provider"), m_provider->id()},
                                                                   {QStringLiteral("message_count"), m_messages.size()},
                                                                   {QStringLiteral("tool_count"), m_toolSurface->schemas().size()}});
    m_reply = m_nam->post(request, QJsonDocument(modelRequest.body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &VibeCutAgent::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &VibeCutAgent::onFinished);
    Q_EMIT statusChanged(QStringLiteral("Thinking…"));
}

void VibeCutAgent::onReadyRead()
{
    if (!m_reply) return;
    const QList<SseParser::Event> events = m_sse.feed(m_reply->readAll());
    for (const SseParser::Event &event : events) handleEvent(event);
}

void VibeCutAgent::handleEvent(const SseParser::Event &event)
{
    if (!m_provider) {
        fail(QStringLiteral("The active VibeCut model provider disappeared during a streaming response."));
        return;
    }
    const QJsonObject object = m_provider->normalizeStreamEvent(event.data);
    if (object.isEmpty()) {
        return;
    }
    const QString type = object.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("content_block_start")) {
        m_curBlock = object.value(QStringLiteral("content_block")).toObject();
        m_curText.clear(); m_curThinking.clear(); m_curJson.clear();
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
        if (blockType == QLatin1String("text")) m_curBlock.insert(QStringLiteral("text"), m_curText);
        else if (blockType == QLatin1String("thinking")) m_curBlock.insert(QStringLiteral("thinking"), m_curThinking);
        else if (blockType == QLatin1String("tool_use")) m_curBlock.insert(QStringLiteral("input"), QJsonDocument::fromJson(m_curJson.toUtf8()).object());
        m_blocks.append(m_curBlock);
        m_curBlock = QJsonObject();
    } else if (type == QLatin1String("message_delta")) {
        const QString stop = object.value(QStringLiteral("delta")).toObject().value(QStringLiteral("stop_reason")).toString();
        if (!stop.isEmpty()) m_stopReason = stop;
    } else if (type == QLatin1String("message_stop")) {
        finishTurn();
    } else if (type == QLatin1String("error")) {
        fail(object.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString(QStringLiteral("stream error")));
    }
}

void VibeCutAgent::onFinished()
{
    if (!m_reply) return;
    const QNetworkReply::NetworkError networkError = m_reply->error();
    const QString networkErrorString = m_reply->errorString();
    const QByteArray trailing = m_reply->readAll();
    if (!trailing.isEmpty()) {
        for (const SseParser::Event &event : m_sse.feed(trailing)) handleEvent(event);
    }

    m_reply->deleteLater();
    m_reply = nullptr;

    if (networkError != QNetworkReply::NoError && !m_turnFinished) {
        QString detail = networkErrorString;
        const QJsonObject errorObject = QJsonDocument::fromJson(trailing).object();
        const QString apiMessage = errorObject.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
        if (!apiMessage.isEmpty()) detail = apiMessage;
        fail(QStringLiteral("Request failed: %1").arg(detail));
        return;
    }
    if (!m_turnFinished) finishTurn();

    if (m_autoApprovePending && m_planRuntime && m_planRuntime->hasPendingPlan() && !m_planRuntime->executing()) {
        m_autoApprovePending = false;
        approvePendingPlanInternal(false, false);
    }
}

void VibeCutAgent::publishToolHook(const QString &eventName, const QString &tool, const QJsonObject &payload)
{
    QJsonObject eventPayload = payload;
    eventPayload.insert(QStringLiteral("tool"), tool);
    m_hooks->publish(eventName, eventPayload);
}

void VibeCutAgent::finishTurn()
{
    if (m_turnFinished) return;
    m_turnFinished = true;
    const bool normalStop = m_stopReason == QLatin1String("end_turn") || m_stopReason.isEmpty();

    if (m_blocks.isEmpty() && normalStop && m_emptyTurnRetries < kMaxEmptyTurnRetries) {
        ++m_emptyTurnRetries;
        qWarning().noquote() << QStringLiteral("[VibeCut] empty end_turn - retrying (%1/%2) (%3)")
                                     .arg(m_emptyTurnRetries).arg(static_cast<int>(kMaxEmptyTurnRetries)).arg(historyDiagnostic());
        Q_EMIT statusChanged(QStringLiteral("Retrying (no response)…"));
        startRequest();
        return;
    }

    if (!m_blocks.isEmpty()) m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), m_blocks}});

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
            if (!isToolUse(block)) continue;
            const QString name = block.value(QStringLiteral("name")).toString();
            if (name == QLatin1String("edit_plan_propose")) hasExplicitPlanProposal = true;
            const auto policy = policies.constFind(name);
            if (policy == policies.constEnd() || policy.value().risk != VibeCutToolRisk::ReadOnly) directMutationBlocks.append(block);
        }

        QJsonObject directProposal;
        if (!directMutationBlocks.isEmpty() && !hasExplicitPlanProposal && m_planRuntime && !m_planRuntime->hasPendingPlan()) {
            directProposal = m_planRuntime->proposeDirectToolCalls(directMutationBlocks, m_currentUserRequest);
        }

        QJsonArray toolResults;
        for (const QJsonValue &value : m_blocks) {
            const QJsonObject block = value.toObject();
            if (!isToolUse(block)) continue;
            const QString name = block.value(QStringLiteral("name")).toString();
            const QString id = block.value(QStringLiteral("id")).toString();
            const QJsonObject input = block.value(QStringLiteral("input")).toObject();
            const auto policy = policies.constFind(name);
            const bool mutation = policy == policies.constEnd() || policy.value().risk != VibeCutToolRisk::ReadOnly;

            QJsonObject result;
            if (mutation) {
                if (m_planRuntime && m_planRuntime->hasPendingPlan()) {
                    result = QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("not_executed"), true},
                                         {QStringLiteral("requires_approval"), m_planRuntime->pendingRequiresConfirmation()},
                                         {QStringLiteral("plan_id"), m_planRuntime->pendingPlanId()},
                                         {QStringLiteral("error"), QStringLiteral("Not executed directly: the requested change is governed by the pending plan runtime.")}};
                } else if (directProposal.value(QStringLiteral("ok")).toBool()) {
                    result = QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("not_executed"), true},
                                         {QStringLiteral("plan_id"), m_planRuntime ? m_planRuntime->pendingPlanId() : QString()},
                                         {QStringLiteral("error"), QStringLiteral("Not executed directly: the requested change was converted into a governed plan.")}};
                } else if (hasExplicitPlanProposal) {
                    result = QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("not_executed"), true},
                                         {QStringLiteral("error"), QStringLiteral("Not executed directly: this turn proposed a governed edit plan.")}};
                } else {
                    result = directProposal.isEmpty() ? QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("not_executed"), true},
                                                                    {QStringLiteral("error"), QStringLiteral("Ungoverned project-changing tool call was blocked.")}}
                                                      : directProposal;
                }
                publishToolHook(QStringLiteral("tool.blocked_direct_mutation"), name, QJsonObject{{QStringLiteral("input"), input},
                                                                                                  {QStringLiteral("result"), result}});
            } else {
                m_anyToolCalledThisExchange = true;
                Q_EMIT toolInvoked(name, QString::fromUtf8(compact(input)));
                publishToolHook(QStringLiteral("tool.invoked"), name, QJsonObject{{QStringLiteral("input"), input}});
                result = m_toolSurface->invoke(name, input);
                Q_EMIT toolCompleted(name, QString::fromUtf8(compact(result)));
                publishToolHook(result.value(QStringLiteral("ok")).toBool() ? QStringLiteral("tool.completed") : QStringLiteral("tool.failed"),
                                name, QJsonObject{{QStringLiteral("result"), result}});
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
            if (m_planRuntime->pendingRequiresConfirmation()) Q_EMIT statusChanged(QStringLiteral("Awaiting plan approval"));
            return;
        }
        startRequest();
        return;
    }

    QString finalText;
    for (const QJsonValue &value : m_blocks) {
        const QJsonObject block = value.toObject();
        if (block.value(QStringLiteral("type")).toString() == QLatin1String("text")) finalText += block.value(QStringLiteral("text")).toString();
    }
    finalText = finalText.trimmed();

    if (m_stopReason != QLatin1String("end_turn") && !m_stopReason.isEmpty()) {
        qWarning().noquote() << QStringLiteral("[VibeCut] turn ended with stop_reason=%1 (%2)").arg(m_stopReason, historyDiagnostic());
        fail(QStringLiteral("Turn ended unexpectedly (%1) instead of finishing normally.").arg(m_stopReason));
        return;
    }
    if (finalText.isEmpty() && !m_anyToolCalledThisExchange && !hasPendingPlan()) {
        fail(QStringLiteral("The model didn't respond or take any action. Try again or rephrase."));
        return;
    }

    Q_EMIT assistantMessage(finalText);
    if (!hasPendingPlan()) Q_EMIT statusChanged(QStringLiteral("Ready"));
}

void VibeCutAgent::fail(const QString &message)
{
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    m_hooks->publish(QStringLiteral("agent.error"), QJsonObject{{QStringLiteral("message"), message}});
    Q_EMIT errorOccurred(message);
    Q_EMIT statusChanged(hasPendingPlan() ? QStringLiteral("Awaiting plan approval") : QStringLiteral("Ready"));
}
