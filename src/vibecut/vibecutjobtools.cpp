/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecutjobtools.h"

#include "vibecutjobmanager.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>
#include <QSet>

namespace {
QHash<VibeCutTools *, QString> s_speechSetupJobs;
QSet<VibeCutTools *> s_speechBridgeInstalled;

QString stateName(VibeCutJobState state)
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

QJsonObject toJson(const VibeCutJob &job)
{
    return QJsonObject{{QStringLiteral("id"), job.id}, {QStringLiteral("kind"), job.kind}, {QStringLiteral("label"), job.label},
                       {QStringLiteral("state"), stateName(job.state)}, {QStringLiteral("progress"), job.progress},
                       {QStringLiteral("message"), job.message}, {QStringLiteral("cancelable"), job.cancelable},
                       {QStringLiteral("terminal"), job.terminal()}};
}

QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

void ensureSpeechJobBridge(VibeCutTools *tools)
{
    if (!tools || s_speechBridgeInstalled.contains(tools)) return;
    s_speechBridgeInstalled.insert(tools);

    QObject::connect(tools, &VibeCutTools::backgroundProgress, tools, [tools](const QString &message) {
        const QString jobId = s_speechSetupJobs.value(tools);
        if (jobId.isEmpty()) return;
        VibeCutJob job;
        if (!tools->jobManager()->job(jobId, job) || job.terminal()) return;

        if (message.contains(QStringLiteral("Whisper is ready"), Qt::CaseInsensitive)) {
            tools->jobManager()->markSucceeded(jobId, message);
            s_speechSetupJobs.remove(tools);
        } else if (message.contains(QStringLiteral("failed"), Qt::CaseInsensitive) ||
                   message.contains(QStringLiteral("could not"), Qt::CaseInsensitive)) {
            tools->jobManager()->markFailed(jobId, message);
            s_speechSetupJobs.remove(tools);
        } else {
            tools->jobManager()->markRunning(jobId, message);
        }
    });
    QObject::connect(tools, &QObject::destroyed, [tools]() {
        s_speechSetupJobs.remove(tools);
        s_speechBridgeInstalled.remove(tools);
    });
}

QString ensureSpeechJob(VibeCutTools *tools, const QString &model)
{
    QString jobId = s_speechSetupJobs.value(tools);
    VibeCutJob existing;
    if (!jobId.isEmpty() && tools->jobManager()->job(jobId, existing) && !existing.terminal()) return jobId;
    jobId = tools->jobManager()->createJob(QStringLiteral("speech_setup"),
                                           QStringLiteral("Set up Whisper %1").arg(model.isEmpty() ? QStringLiteral("model") : model), false);
    s_speechSetupJobs.insert(tools, jobId);
    tools->jobManager()->markRunning(jobId, QStringLiteral("Whisper setup is starting."));
    return jobId;
}
} // namespace

bool registerVibeCutJobTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("job tools require a native VibeCut runtime");
        return false;
    }
    ensureSpeechJobBridge(tools);

    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    const QJsonObject listSchema{{QStringLiteral("name"), QStringLiteral("jobs_list")},
                                 {QStringLiteral("description"), QStringLiteral("List VibeCut background jobs and their current progress/state. Read-only.")},
                                 {QStringLiteral("input_schema"), noArgs}};
    VibeCutToolPolicy listPolicy;
    listPolicy.name = QStringLiteral("jobs_list");
    listPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(listSchema, listPolicy, [tools](const QJsonObject &) {
            QJsonArray jobs;
            for (const VibeCutJob &job : tools->jobManager()->jobs()) jobs.append(toJson(job));
            return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("jobs"), jobs}};
        }, error)) return false;

    const QJsonObject statusInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{{QStringLiteral("job_id"),
                                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                  {QStringLiteral("description"), QStringLiteral("Stable job id returned by an async VibeCut tool.")}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("job_id")}},
                                  {QStringLiteral("additionalProperties"), false}};
    const QJsonObject statusSchema{{QStringLiteral("name"), QStringLiteral("job_status")},
                                   {QStringLiteral("description"), QStringLiteral("Get current state and progress for one VibeCut background job. Read-only.")},
                                   {QStringLiteral("input_schema"), statusInput}};
    VibeCutToolPolicy statusPolicy;
    statusPolicy.name = QStringLiteral("job_status");
    statusPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(statusSchema, statusPolicy, [tools](const QJsonObject &input) {
            const QString id = input.value(QStringLiteral("job_id")).toString().trimmed();
            if (id.isEmpty()) return err(QStringLiteral("job_id must not be empty"));
            VibeCutJob job;
            if (!tools->jobManager()->job(id, job)) return err(QStringLiteral("Unknown VibeCut job: %1").arg(id));
            return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("job"), toJson(job)}};
        }, error)) return false;

    const QJsonObject cancelSchema{{QStringLiteral("name"), QStringLiteral("job_cancel")},
                                   {QStringLiteral("description"),
                                    QStringLiteral("Request cancellation of one cancelable VibeCut background job. This is a governed external control action; it never edits the timeline directly. The underlying job must acknowledge the cancellation and reach a terminal cancelled/failed state.")},
                                   {QStringLiteral("input_schema"), statusInput}};
    VibeCutToolPolicy cancelPolicy;
    cancelPolicy.name = QStringLiteral("job_cancel");
    cancelPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    cancelPolicy.mutatesProject = false;
    if (!surface.registerTool(cancelSchema, cancelPolicy, [tools](const QJsonObject &input) {
            const QString id = input.value(QStringLiteral("job_id")).toString().trimmed();
            if (id.isEmpty()) return err(QStringLiteral("job_id must not be empty"));
            VibeCutJob job;
            if (!tools->jobManager()->job(id, job)) return err(QStringLiteral("Unknown VibeCut job: %1").arg(id));
            if (job.terminal()) return err(QStringLiteral("Job %1 is already terminal (%2).").arg(id, stateName(job.state)));
            if (!job.cancelable) return err(QStringLiteral("Job %1 does not support cancellation.").arg(id));
            if (!tools->jobManager()->requestCancel(id)) return err(QStringLiteral("Could not request cancellation for job %1.").arg(id));
            VibeCutJob updated;
            tools->jobManager()->job(id, updated);
            return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("cancel_requested"), true},
                               {QStringLiteral("job"), toJson(updated)}};
        }, error)) return false;

    QJsonObject speechInput{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{{QStringLiteral("model"),
                                QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                            {QStringLiteral("description"), QStringLiteral("Whisper model name. Defaults to turbo.")}}}}},
                            {QStringLiteral("additionalProperties"), false}};
    const QJsonObject speechSchema{{QStringLiteral("name"), QStringLiteral("speech_setup")},
                                   {QStringLiteral("description"), QStringLiteral("Set up Whisper in the background. Returns a stable VibeCut job_id so approved compound plans can wait for setup before continuing.")},
                                   {QStringLiteral("input_schema"), speechInput}};
    VibeCutToolPolicy speechPolicy = tools->policies().value(QStringLiteral("speech_setup"));
    VibeCutToolSurface *surfacePtr = &surface;
    return surface.overrideBaseTool(speechSchema, speechPolicy, [tools, surfacePtr](const QJsonObject &input) {
        const QString model = input.value(QStringLiteral("model")).toString(QStringLiteral("turbo"));
        const QJsonObject status = surfacePtr->invokeBase(QStringLiteral("speech_status"), QJsonObject{});
        if (status.value(QStringLiteral("setup_in_progress")).toBool()) {
            const QString jobId = ensureSpeechJob(tools, model);
            return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true},
                               {QStringLiteral("job_id"), jobId}, {QStringLiteral("model"), model},
                               {QStringLiteral("note"), QStringLiteral("Whisper setup was already in progress; attached to its VibeCut job.")}};
        }

        QJsonObject result = surfacePtr->invokeBase(QStringLiteral("speech_setup"), input);
        if (!result.value(QStringLiteral("ok")).toBool() || result.value(QStringLiteral("already_installed")).toBool()) return result;
        if (result.value(QStringLiteral("started")).toBool()) {
            const QString jobId = ensureSpeechJob(tools, model);
            result.insert(QStringLiteral("job_id"), jobId);
        }
        return result;
    }, error);
}
