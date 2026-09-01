/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecutdock.h"
#include "vibecutagent.h"
#include "vibecutsecretstore.h"
#include "vibecuttools.h"

#include "core.h"
#include "mainwindow.h"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTextBrowser>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>

namespace {
const QString kNoisePrompt = QStringLiteral("Remove background noise from the selected clip.");

struct Suggestion { QString id; QString label; QString prompt; };

const QVector<Suggestion> &suggestions()
{
    static const QVector<Suggestion> list = {
        {QStringLiteral("denoise"), QStringLiteral("Remove background noise from the selected clip"), kNoisePrompt},
        {QStringLiteral("subtitles"), QStringLiteral("Generate subtitles"),
         QStringLiteral("Generate subtitles. Prefer the selected clip; ask me before using the whole project if the scope is ambiguous. Set up Whisper first if needed.")},
        {QStringLiteral("list-clips"), QStringLiteral("What clips are on my timeline?"), QStringLiteral("List the clips on my timeline.")},
        {QStringLiteral("help"), QStringLiteral("What can you help me with?"), QStringLiteral("What can you help me with right now?")},
    };
    return list;
}

TimelineController *currentTimelineController()
{
    if (!pCore || !pCore->window()) return nullptr;
    TimelineWidget *timeline = pCore->window()->getCurrentTimeline();
    return timeline ? timeline->controller() : nullptr;
}
} // namespace

VibeCutDock::VibeCutDock(QWidget *parent)
    : QWidget(parent)
    , m_transcript(new QTextBrowser(this))
    , m_status(new QLabel(this))
    , m_progress(new QProgressBar(this))
    , m_trustMode(new QComboBox(this))
    , m_credentials(new QPushButton(i18n("Credentials"), this))
    , m_newChat(new QPushButton(i18n("New Chat"), this))
    , m_approvePlan(new QPushButton(i18n("Approve Plan"), this))
    , m_cancelPlan(new QPushButton(i18n("Cancel"), this))
    , m_input(new QLineEdit(this))
    , m_send(new QPushButton(i18n("Send"), this))
    , m_tools(new VibeCutTools(this))
    , m_agent(new VibeCutAgent(m_tools, this))
{
    setObjectName(QStringLiteral("VibeCutDock"));
    m_transcript->setReadOnly(true);
    m_transcript->setAcceptRichText(false);
    m_transcript->setOpenLinks(false);
    m_transcript->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_input->setPlaceholderText(i18n("Ask VibeCut to edit the timeline…"));

    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->setMaximumHeight(4);
    m_progress->setVisible(false);

    m_trustMode->addItem(i18n("Review"));
    m_trustMode->addItem(i18n("Auto"));
    m_trustMode->addItem(i18n("Turbo"));
    m_trustMode->setToolTip(i18n("Review: approve every side effect. Auto: reversible edits can auto-run; major/external changes still ask. Turbo: governed changes auto-run except explicitly confirmation-required or irreversible actions."));
    m_trustMode->setCurrentIndex(0);

    m_credentials->setToolTip(i18n("Store or replace the Anthropic API key in KWallet when available. Environment variables remain supported."));
    m_credentials->setFlat(true);
    m_newChat->setToolTip(i18n("Start a fresh conversation. Long VibeCut sessions are compacted automatically at complete exchange boundaries."));
    m_newChat->setFlat(true);
    m_approvePlan->setToolTip(i18n("Execute the reviewed plan against the current project revision."));
    m_cancelPlan->setToolTip(i18n("Discard the proposed plan without changing the project."));
    m_approvePlan->setVisible(false);
    m_cancelPlan->setVisible(false);

    auto *statusRow = new QHBoxLayout;
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(m_progress, 1);
    statusRow->addWidget(m_trustMode);
    statusRow->addWidget(m_credentials);
    statusRow->addWidget(m_newChat);

    auto *planRow = new QHBoxLayout;
    planRow->addStretch(1);
    planRow->addWidget(m_cancelPlan);
    planRow->addWidget(m_approvePlan);

    auto *inputRow = new QHBoxLayout;
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_send);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_transcript, 1);
    layout->addLayout(statusRow);
    layout->addLayout(planRow);
    layout->addLayout(inputRow);

    connect(m_send, &QPushButton::clicked, this, &VibeCutDock::submit);
    connect(m_credentials, &QPushButton::clicked, this, &VibeCutDock::manageCredentials);
    connect(m_newChat, &QPushButton::clicked, this, &VibeCutDock::newChat);
    connect(m_approvePlan, &QPushButton::clicked, m_agent, &VibeCutAgent::approvePendingPlan);
    connect(m_cancelPlan, &QPushButton::clicked, m_agent, &VibeCutAgent::cancelPendingPlan);
    connect(m_input, &QLineEdit::returnPressed, this, &VibeCutDock::submit);
    connect(m_transcript, &QTextBrowser::anchorClicked, this, &VibeCutDock::onSuggestionClicked);
    connect(m_trustMode, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int index) {
        const VibeCutTrustMode mode = index == 1 ? VibeCutTrustMode::Auto : (index == 2 ? VibeCutTrustMode::Turbo : VibeCutTrustMode::Off);
        m_agent->setTrustMode(mode);
    });
    connect(m_agent, &VibeCutAgent::trustModeChanged, this, [this](VibeCutTrustMode mode) {
        const QSignalBlocker blocker(m_trustMode);
        m_trustMode->setCurrentIndex(mode == VibeCutTrustMode::Auto ? 1 : (mode == VibeCutTrustMode::Turbo ? 2 : 0));
        appendLine(i18n("Trust mode: %1", m_trustMode->currentText()), QStringLiteral("#888"));
    });

    connect(m_agent, &VibeCutAgent::statusChanged, this, [this](const QString &status) {
        m_status->setText(status);
        setBusyUi(m_agent->busy());
    });
    connect(m_agent, &VibeCutAgent::assistantTextDelta, this, [this](const QString &text) {
        if (!m_streamStarted) {
            m_transcript->append(QStringLiteral("VibeCut: "));
            m_streamStarted = true;
        }
        m_transcript->moveCursor(QTextCursor::End);
        m_transcript->insertPlainText(text);
        m_transcript->moveCursor(QTextCursor::End);
    });
    connect(m_agent, &VibeCutAgent::assistantMessage, this, [this](const QString &text) {
        if (!m_streamStarted) {
            appendLine(text.isEmpty() ? i18n("✓ Finished (see verified steps above).") : QStringLiteral("VibeCut: %1").arg(text),
                       text.isEmpty() ? QStringLiteral("#2a8") : QString());
        }
        m_streamStarted = false;
        if (!m_agent->hasPendingPlan()) appendNextStepSuggestions();
    });
    connect(m_agent, &VibeCutAgent::toolInvoked, this, [this](const QString &name, const QString &args) {
        const QString friendly = describeTool(name, args);
        if (!friendly.isEmpty()) appendLine(friendly, QStringLiteral("#888"));
    });
    connect(m_agent, &VibeCutAgent::toolFailed, this, [this](const QString &name, const QString &error) {
        appendLine(i18n("⚠ %1 failed: %2", name, error), QStringLiteral("#c33"));
        if (error.contains(QStringLiteral("not set up"))) offerSpeechSetup();
    });
    connect(m_agent, &VibeCutAgent::toolCompleted, this, [this](const QString &name, const QString &resultJson) {
        const QString summary = describeToolResult(name, resultJson);
        if (!summary.isEmpty()) appendLine(summary, QStringLiteral("#888"));
        if (name == QLatin1String("speech_status")) {
            const QJsonObject result = QJsonDocument::fromJson(resultJson.toUtf8()).object();
            if (result.value(QStringLiteral("ok")).toBool() && !result.value(QStringLiteral("dependencies_installed")).toBool()) offerSpeechSetup();
        }
    });
    connect(m_agent, &VibeCutAgent::userQuestionRaised, this, [this](const QString &question) {
        appendLine(QStringLiteral("VibeCut asks: %1").arg(question), QStringLiteral("#c80"));
    });
    connect(m_agent, &VibeCutAgent::backgroundProgress, this, [this](const QString &message) {
        appendLine(QStringLiteral("⏳ %1").arg(message), QStringLiteral("#888"));
    });
    connect(m_agent, &VibeCutAgent::errorOccurred, this, [this](const QString &error) {
        appendLine(QStringLiteral("⚠ %1").arg(error), QStringLiteral("#c33"));
        m_streamStarted = false;
        setBusyUi(m_agent->busy());
    });
    connect(m_agent, &VibeCutAgent::planProposed, this, [this](const QString &planId, const QString &summary) {
        m_pendingPlanSummary = summary;
        appendLine(i18n("Review plan %1:\n%2", planId, summary), QStringLiteral("#c80"));
        appendLine(i18n("No project changes have been made yet."), QStringLiteral("#888"));
        setPlanReviewVisible(true);
        setBusyUi(false);
    });
    connect(m_agent, &VibeCutAgent::planProgress, this, [this](const QString &message) {
        appendLine(QStringLiteral("▶ %1").arg(message), QStringLiteral("#888"));
        setPlanReviewVisible(false);
        setBusyUi(m_agent->busy());
    });
    connect(m_agent, &VibeCutAgent::planFinished, this, [this](const QString &, bool success, const QString &summary) {
        setPlanReviewVisible(false);
        m_pendingPlanSummary.clear();
        appendLine(success ? i18n("✓ %1", summary) : i18n("■ %1", summary), success ? QStringLiteral("#2a8") : QStringLiteral("#c80"));
        setBusyUi(false);
        appendNextStepSuggestions();
    });

    if (!m_agent->hasApiKey()) {
        appendLine(i18n("VibeCut model provider is not configured. Use Credentials to store an Anthropic key in KWallet when available, or set ANTHROPIC_API_KEY in the environment."), QStringLiteral("#c33"));
        m_input->setEnabled(false);
        m_send->setEnabled(false);
        m_status->setText(QStringLiteral("No model provider"));
    } else {
        appendWelcome();
        m_status->setText(i18n("Ready · %1", m_agent->modelProviderId()));
    }
}

void VibeCutDock::appendWelcome()
{
    QString html = QStringLiteral("<b>%1</b><br>%2<br>")
                       .arg(i18n("Hi, I'm VibeCut."), i18n("I can inspect the live project immediately and route changes through governed, reviewable plans. Try one of these:"));
    for (const Suggestion &suggestion : suggestions()) html += QStringLiteral("• <a href=\"vibecut://%1\">%2</a><br>").arg(suggestion.id, suggestion.label.toHtmlEscaped());
    m_transcript->append(html);
    m_transcript->moveCursor(QTextCursor::End);
}

void VibeCutDock::onSuggestionClicked(const QUrl &url)
{
    const QString id = url.host();
    if (id == QLatin1String("speech-install")) { sendPrompt(QStringLiteral("Set up Whisper speech-to-text with the recommended model.")); return; }
    if (id == QLatin1String("speech-settings")) {
        if (pCore && pCore->window()) pCore->window()->slotShowPreferencePage(Kdenlive::PageSpeech);
        return;
    }
    if (id == QLatin1String("search-subtitles")) {
        if (!m_agent->busy() && !m_agent->hasPendingPlan()) {
            m_input->setText(QStringLiteral("Find where I say "));
            m_input->setFocus();
            m_input->setCursorPosition(m_input->text().size());
        }
        return;
    }
    if (id == QLatin1String("jobs")) {
        if (!m_agent->busy() && !m_agent->hasPendingPlan()) sendPrompt(QStringLiteral("Show me the current VibeCut background jobs and their status."));
        return;
    }
    if (m_agent->busy() || m_agent->hasPendingPlan()) return;
    if (id == QLatin1String("denoise")) { runNoiseSuggestion(); return; }
    for (const Suggestion &suggestion : suggestions()) {
        if (suggestion.id == id) { cancelPendingSelection(); sendPrompt(suggestion.prompt); return; }
    }
}

void VibeCutDock::offerSpeechSetup()
{
    appendLine(i18n("Whisper speech-to-text isn't set up. <a href=\"vibecut://speech-install\">Prepare a setup plan</a> · <a href=\"vibecut://speech-settings\">Open Speech-to-Text settings</a>"));
}

void VibeCutDock::submit()
{
    const QString text = m_input->text().trimmed();
    if (text.isEmpty() || m_agent->busy() || m_agent->hasPendingPlan()) return;
    m_input->clear();
    cancelPendingSelection();
    sendPrompt(text);
}

void VibeCutDock::newChat()
{
    cancelPendingSelection();
    m_agent->resetConversation();
    m_streamStarted = false;
    m_pendingPlanSummary.clear();
    setPlanReviewVisible(false);
    m_transcript->clear();
    if (m_agent->hasApiKey()) appendWelcome();
}

void VibeCutDock::manageCredentials()
{
    if (m_agent->busy() || m_agent->hasPendingPlan()) {
        appendLine(i18n("Finish the current VibeCut operation before changing credentials."), QStringLiteral("#c80"));
        return;
    }
    if (!VibeCutSecretStore::available()) {
        appendLine(i18n("KWallet is not available in this build. Set ANTHROPIC_API_KEY in the environment instead."), QStringLiteral("#c80"));
        return;
    }

    bool accepted = false;
    const QString key = QInputDialog::getText(this, i18n("VibeCut Credentials"), i18n("Anthropic API key:"), QLineEdit::Password,
                                              QString(), &accepted).trimmed();
    if (!accepted || key.isEmpty()) return;

    QString error;
    if (!VibeCutSecretStore::writeSecret(QStringLiteral("anthropic_api_key"), key, &error)) {
        appendLine(i18n("⚠ Could not store the key: %1", error), QStringLiteral("#c33"));
        return;
    }
    if (!m_agent->reloadModelProvider(&error)) {
        appendLine(i18n("⚠ Key stored, but the provider could not reload: %1", error), QStringLiteral("#c33"));
        return;
    }

    appendLine(i18n("✓ Anthropic API key stored in KWallet and provider reloaded."), QStringLiteral("#2a8"));
    if (m_transcript->document()->isEmpty()) appendWelcome();
    setBusyUi(false);
}

void VibeCutDock::runNoiseSuggestion()
{
    cancelPendingSelection();
    if (m_tools->selectedClipId() != -1) { sendPrompt(kNoisePrompt); return; }
    TimelineController *controller = currentTimelineController();
    if (!controller) {
        appendLine(i18n("Open a project and add a clip to the timeline first."), QStringLiteral("#c33"));
        return;
    }
    m_pendingPrompt = kNoisePrompt;
    m_awaitingSelection = true;
    m_status->setText(i18n("Waiting for a clip…"));
    appendLine(i18n("No clip selected — click the clip with your audio in the timeline and I'll prepare the edit plan automatically."), QStringLiteral("#c80"));
    m_selectionConn = connect(controller, &TimelineController::selectionChanged, this, [this]() {
        if (!m_awaitingSelection || m_tools->selectedClipId() == -1) return;
        const QString prompt = m_pendingPrompt;
        cancelPendingSelection();
        sendPrompt(prompt);
    });
}

void VibeCutDock::cancelPendingSelection()
{
    m_awaitingSelection = false;
    m_pendingPrompt.clear();
    if (m_selectionConn) { disconnect(m_selectionConn); m_selectionConn = {}; }
}

void VibeCutDock::sendPrompt(const QString &text)
{
    appendLine(QStringLiteral("You: %1").arg(text), QStringLiteral("#39c"));
    m_streamStarted = false;
    m_agent->sendUserMessage(text);
}

void VibeCutDock::setBusyUi(bool busy)
{
    const bool hasProvider = m_agent->hasApiKey();
    const bool reviewing = m_agent->hasPendingPlan();
    m_input->setEnabled(hasProvider && !busy && !reviewing);
    m_send->setEnabled(hasProvider && !busy && !reviewing);
    m_credentials->setEnabled(!busy && !reviewing);
    m_newChat->setEnabled(!busy);
    m_trustMode->setEnabled(!busy && !reviewing);
    m_progress->setVisible(busy);
    m_approvePlan->setEnabled(reviewing && !busy);
    m_cancelPlan->setEnabled(reviewing && !busy);
    if (!busy && !reviewing && hasProvider) m_input->setFocus();
}

void VibeCutDock::setPlanReviewVisible(bool visible)
{
    m_approvePlan->setVisible(visible);
    m_cancelPlan->setVisible(visible);
}

void VibeCutDock::appendNextStepSuggestions()
{
    if (!m_agent->hasApiKey() || m_agent->busy() || m_agent->hasPendingPlan()) return;
    m_transcript->append(i18n("Next: <a href=\"vibecut://search-subtitles\">search project media/transcript</a> · <a href=\"vibecut://list-clips\">review timeline clips</a> · <a href=\"vibecut://jobs\">check background jobs</a>"));
    m_transcript->moveCursor(QTextCursor::End);
}

QString VibeCutDock::describeTool(const QString &name, const QString &argsJson) const
{
    if (name == QLatin1String("timeline_list_clips")) return i18n("Looking at the clips on your timeline…");
    if (name == QLatin1String("timeline_get_selection")) return i18n("Checking what's selected…");
    if (name == QLatin1String("subtitles_search")) return i18n("Searching the existing subtitles…");
    if (name == QLatin1String("media_search")) return i18n("Searching the project media index…");
    if (name == QLatin1String("jobs_list")) return i18n("Checking VibeCut background jobs…");
    if (name == QLatin1String("edit_plan_propose")) return i18n("Preparing a governed edit plan…");
    if (name == QLatin1String("vibescript_plan")) return i18n("Evaluating a bounded VibeScript plan…");
    if (name == QLatin1String("ask_user")) return QString();
    if (name == QLatin1String("speech_status")) return i18n("Checking speech-to-text status…");
    if (name == QLatin1String("speech_setup")) return i18n("Preparing Whisper setup…");
    if (name == QLatin1String("generate_subtitles")) return i18n("Starting the approved subtitle pipeline…");
    if (name == QLatin1String("effect_apply")) {
        static const QHash<QString, QString> friendlyNames = {{QStringLiteral("denoise"), i18n("AI Noise Removal (DeepFilterNet)")},
                                                              {QStringLiteral("denoise_light"), i18n("Noise Suppressor (RNNoise)")}};
        const QJsonObject args = QJsonDocument::fromJson(argsJson.toUtf8()).object();
        const QString key = args.value(QStringLiteral("effect")).toString();
        return i18n("Adding \"%1\"…", friendlyNames.value(key, key));
    }
    return i18n("Running %1…", name);
}

QString VibeCutDock::describeToolResult(const QString &name, const QString &resultJson) const
{
    const QJsonObject result = QJsonDocument::fromJson(resultJson.toUtf8()).object();
    if (!result.value(QStringLiteral("ok")).toBool()) return QString();
    if (name == QLatin1String("timeline_get_selection")) {
        const int clipId = result.value(QStringLiteral("selected_clip_id")).toInt(-1);
        return clipId == -1 ? i18n("→ Nothing is selected on the timeline.") : i18n("→ Clip %1 is selected.", clipId);
    }
    if (name == QLatin1String("timeline_list_clips")) return i18n("→ %1 clip(s) on the timeline.", result.value(QStringLiteral("clips")).toArray().size());
    if (name == QLatin1String("subtitles_search")) return i18n("→ %1 subtitle match(es).", result.value(QStringLiteral("match_count")).toInt());
    if (name == QLatin1String("media_search")) return i18n("→ %1 ranked media hit(s).", result.value(QStringLiteral("hits")).toArray().size());
    if (name == QLatin1String("jobs_list")) return i18n("→ %1 VibeCut job(s).", result.value(QStringLiteral("jobs")).toArray().size());
    if (name == QLatin1String("speech_status")) {
        const bool ready = result.value(QStringLiteral("dependencies_installed")).toBool();
        const int models = result.value(QStringLiteral("models_installed")).toArray().size();
        return ready ? i18n("→ Whisper is ready (%1 model(s) installed).", models) : i18n("→ Whisper is not set up yet.");
    }
    if (name == QLatin1String("edit_plan_propose") || name == QLatin1String("vibescript_plan")) {
        return i18n("→ Plan prepared; governance decides whether review is required before execution.");
    }
    return QString();
}

void VibeCutDock::appendLine(const QString &text, const QString &cssColor)
{
    if (cssColor.isEmpty()) m_transcript->append(text);
    else m_transcript->append(QStringLiteral("<span style=\"color:%1\">%2</span>").arg(cssColor, text.toHtmlEscaped()));
    m_transcript->moveCursor(QTextCursor::End);
}
