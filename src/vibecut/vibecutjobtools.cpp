/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecutjobtools.h"

#include "vibecutjobmanager.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>

namespace {
QString stateName(VibeCutJobState state)
{
    switch (state) {
    case VibeCutJobState::Queued:
        return QStringLiteral("queued");
    case VibeCutJobState::Running:
        return QStringLiteral("running");
    case VibeCutJobState::CancelRequested:
        return QStringLiteral("cancel_requested");
    case VibeCutJobState::Succeeded:
        return QStringLiteral("succeeded");
    case VibeCutJobState::Failed:
        return QStringLiteral("failed");
    case VibeCutJobState::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("unknown");
}

QJsonObject toJson(const VibeCutJob &job)
{
    return QJsonObject{
        {QStringLiteral("id"), job.id},
        {QStringLiteral("kind"), job.kind},
        {QStringLiteral("label"), job.label},
        {QStringLiteral("state"), stateName(job.state)},
        {QStringLiteral("progress"), job.progress},
        {QStringLiteral("message"), job.message},
        {QStringLiteral("cancelable"), job.cancelable},
        {QStringLiteral("terminal"), job.terminal()},
    };
}

QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}
} // namespace

bool registerVibeCutJobTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) {
            *error = QStringLiteral("job tools require a native VibeCut runtime");
        }
        return false;
    }

    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    const QJsonObject listSchema{
        {QStringLiteral("name"), QStringLiteral("jobs_list")},
        {QStringLiteral("description"), QStringLiteral("List VibeCut background jobs and their current progress/state. Read-only.")},
        {QStringLiteral("input_schema"), noArgs},
    };
    VibeCutToolPolicy listPolicy;
    listPolicy.name = QStringLiteral("jobs_list");
    listPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(listSchema, listPolicy,
                              [tools](const QJsonObject &) {
                                  QJsonArray jobs;
                                  for (const VibeCutJob &job : tools->jobManager()->jobs()) {
                                      jobs.append(toJson(job));
                                  }
                                  return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("jobs"), jobs}};
                              },
                              error)) {
        return false;
    }

    const QJsonObject statusInput{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("job_id"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("description"), QStringLiteral("Stable job id returned by an async VibeCut tool.")}}}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("job_id")}},
        {QStringLiteral("additionalProperties"), false},
    };
    const QJsonObject statusSchema{
        {QStringLiteral("name"), QStringLiteral("job_status")},
        {QStringLiteral("description"), QStringLiteral("Get current state and progress for one VibeCut background job. Read-only.")},
        {QStringLiteral("input_schema"), statusInput},
    };
    VibeCutToolPolicy statusPolicy;
    statusPolicy.name = QStringLiteral("job_status");
    statusPolicy.risk = VibeCutToolRisk::ReadOnly;

    return surface.registerTool(statusSchema, statusPolicy,
                                [tools](const QJsonObject &input) {
                                    const QString id = input.value(QStringLiteral("job_id")).toString().trimmed();
                                    if (id.isEmpty()) {
                                        return err(QStringLiteral("job_id must not be empty"));
                                    }
                                    VibeCutJob job;
                                    if (!tools->jobManager()->job(id, job)) {
                                        return err(QStringLiteral("Unknown VibeCut job: %1").arg(id));
                                    }
                                    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("job"), toJson(job)}};
                                },
                                error);
}
