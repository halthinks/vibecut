/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutpreflighttools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <QFileInfo>
#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

std::shared_ptr<TimelineItemModel> currentTimeline()
{
    if (!pCore || !pCore->window()) return nullptr;
    TimelineWidget *timeline = pCore->window()->getCurrentTimeline();
    return timeline ? timeline->model() : nullptr;
}
} // namespace

QJsonObject vibeCutProjectPreflight()
{
    if (!pCore || !pCore->currentDoc()) return err(QStringLiteral("No project document is open."));
    const std::shared_ptr<ProjectItemModel> bin = pCore->projectItemModel();
    if (!bin) return err(QStringLiteral("Project bin model is unavailable."));

    QJsonArray blockers;
    QJsonArray warnings;
    QJsonArray missingAssets;
    int fileBacked = 0;
    int generated = 0;
    int proxyOnly = 0;

    for (const QString &binId : bin->getAllClipIds()) {
        const std::shared_ptr<ProjectClip> clip = bin->getClipByBinID(binId);
        if (!clip) continue;
        if (!clip->hasUrl()) {
            ++generated;
            continue;
        }
        ++fileBacked;
        const FileStatus::ClipStatus status = clip->clipStatus();
        const bool missing = !clip->sourceExists() || status == FileStatus::StatusMissing || status == FileStatus::StatusProxyOnly;
        if (status == FileStatus::StatusProxyOnly) ++proxyOnly;
        if (missing) {
            const QString path = QFileInfo(clip->url()).absoluteFilePath();
            missingAssets.append(QJsonObject{{QStringLiteral("bin_id"), binId},
                                             {QStringLiteral("name"), clip->getProducerProperty(QStringLiteral("kdenlive:clipname"))},
                                             {QStringLiteral("path"), path},
                                             {QStringLiteral("clip_status"), static_cast<int>(status)},
                                             {QStringLiteral("has_proxy"), clip->hasProxy()},
                                             {QStringLiteral("timeline_instances"), clip->timelineInstances().size()}});
        }
    }

    if (!missingAssets.isEmpty()) {
        blockers.append(QJsonObject{{QStringLiteral("code"), QStringLiteral("missing_media")},
                                    {QStringLiteral("message"), QStringLiteral("One or more file-backed bin assets are missing or proxy-only.")},
                                    {QStringLiteral("count"), missingAssets.size()}});
    }

    const std::shared_ptr<TimelineItemModel> timeline = currentTimeline();
    if (!timeline) {
        blockers.append(QJsonObject{{QStringLiteral("code"), QStringLiteral("no_active_timeline")},
                                    {QStringLiteral("message"), QStringLiteral("No active timeline is available for editing/render preflight.")}});
    } else if (timeline->isLoading) {
        blockers.append(QJsonObject{{QStringLiteral("code"), QStringLiteral("timeline_loading")},
                                    {QStringLiteral("message"), QStringLiteral("The active timeline is still loading.")}});
    }

    if (proxyOnly > 0) {
        warnings.append(QJsonObject{{QStringLiteral("code"), QStringLiteral("proxy_only_media")},
                                    {QStringLiteral("message"), QStringLiteral("Some assets are available only through proxies; restore originals for a full-quality master export.")},
                                    {QStringLiteral("count"), proxyOnly}});
    }

    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("ready_for_long_jobs"), blockers.isEmpty()},
                       {QStringLiteral("blockers"), blockers},
                       {QStringLiteral("warnings"), warnings},
                       {QStringLiteral("missing_assets"), missingAssets},
                       {QStringLiteral("file_backed_assets"), fileBacked},
                       {QStringLiteral("generated_assets"), generated}};

    if (timeline) {
        result.insert(QStringLiteral("timeline_loading"), timeline->isLoading);
        result.insert(QStringLiteral("track_count"), timeline->getTracksCount());
        result.insert(QStringLiteral("clip_count"), timeline->getClipsCount());
        result.insert(QStringLiteral("composition_count"), timeline->getCompositionsCount());
        result.insert(QStringLiteral("duration_frames"), timeline->duration());
    }
    return result;
}

bool registerVibeCutPreflightTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("project_preflight")},
                             {QStringLiteral("description"), QStringLiteral("Inspect whether the current Kdenlive project is healthy enough for long-running work such as render/export. Reports missing/proxy-only media, active timeline readiness, project counts, blockers and warnings without mutating the project.")},
                             {QStringLiteral("input_schema"), noArgs}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("project_preflight");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(schema, policy, [](const QJsonObject &) { return vibeCutProjectPreflight(); }, error);
}
