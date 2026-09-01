/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutroutingtools.h"

#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

TimelineController *controller()
{
    if (!pCore || !pCore->window()) return nullptr;
    TimelineWidget *timeline = pCore->window()->getCurrentTimeline();
    return timeline ? timeline->controller() : nullptr;
}

std::shared_ptr<TimelineItemModel> model()
{
    TimelineController *ctl = controller();
    return ctl ? ctl->getModel() : nullptr;
}

QJsonObject routingStatus(const QJsonObject &input)
{
    TimelineController *ctl = controller();
    const std::shared_ptr<TimelineItemModel> timeline = model();
    if (!ctl || !timeline) return err(QStringLiteral("No timeline is open."));

    QJsonArray audioTargets;
    for (const QVariant &value : ctl->audioTarget()) {
        const int trackId = value.toInt();
        audioTargets.append(QJsonObject{{QStringLiteral("track_id"), trackId},
                                        {QStringLiteral("track_name"), timeline->isTrack(trackId) ? timeline->getTrackFullName(trackId) : QString()},
                                        {QStringLiteral("stream_name"), ctl->audioTargetName(trackId)}});
    }

    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("video_target_track_id"), ctl->videoTarget()},
                       {QStringLiteral("has_video_target"), ctl->hasVideoTarget()},
                       {QStringLiteral("audio_targets"), audioTargets},
                       {QStringLiteral("audio_target_capacity"), ctl->hasAudioTarget()}};

    if (input.contains(QStringLiteral("audio_track_id"))) {
        const int trackId = input.value(QStringLiteral("audio_track_id")).toInt(-1);
        if (!timeline->isTrack(trackId) || !timeline->isAudioTrack(trackId)) {
            return err(QStringLiteral("audio_track_id %1 must be an existing audio track.").arg(trackId));
        }
        int activeStream = -1;
        const QMap<int, QString> available = ctl->getCurrentTargets(trackId, activeStream);
        QJsonArray streams;
        for (auto it = available.cbegin(); it != available.cend(); ++it) {
            streams.append(QJsonObject{{QStringLiteral("stream"), it.key()}, {QStringLiteral("name"), it.value()}});
        }
        result.insert(QStringLiteral("queried_audio_track_id"), trackId);
        result.insert(QStringLiteral("active_stream"), activeStream);
        result.insert(QStringLiteral("available_streams"), streams);
    }
    return result;
}

QJsonObject setAudioTarget(const QJsonObject &input)
{
    TimelineController *ctl = controller();
    const std::shared_ptr<TimelineItemModel> timeline = model();
    if (!ctl || !timeline) return err(QStringLiteral("No timeline is open."));
    const int trackId = input.value(QStringLiteral("track_id")).toInt(-1);
    const int stream = input.value(QStringLiteral("stream")).toInt(-1);
    if (!timeline->isTrack(trackId) || !timeline->isAudioTrack(trackId)) {
        return err(QStringLiteral("track_id %1 must be an existing audio track.").arg(trackId));
    }

    int activeStream = -1;
    const QMap<int, QString> available = ctl->getCurrentTargets(trackId, activeStream);
    if (!available.contains(stream)) {
        return err(QStringLiteral("Audio stream %1 is not currently assignable to track %2. Call routing_status with audio_track_id first.")
                       .arg(stream).arg(trackId));
    }
    ctl->assignAudioTarget(trackId, stream);

    int verifiedStream = -1;
    ctl->getCurrentTargets(trackId, verifiedStream);
    if (verifiedStream != stream || !ctl->audioTarget().contains(trackId)) {
        return err(QStringLiteral("Audio target assignment did not verify on the live timeline controller."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("track_id"), trackId},
                       {QStringLiteral("stream"), stream}, {QStringLiteral("stream_name"), available.value(stream)},
                       {QStringLiteral("verified"), true}};
}

QJsonObject setVideoTarget(const QJsonObject &input)
{
    TimelineController *ctl = controller();
    const std::shared_ptr<TimelineItemModel> timeline = model();
    if (!ctl || !timeline) return err(QStringLiteral("No timeline is open."));
    const int trackId = input.value(QStringLiteral("track_id")).toInt(-1);
    if (trackId != -1 && (!timeline->isTrack(trackId) || timeline->isAudioTrack(trackId))) {
        return err(QStringLiteral("track_id must be -1 to clear the target or an existing video track."));
    }
    ctl->setVideoTarget(trackId);
    if (ctl->videoTarget() != trackId) {
        return err(QStringLiteral("Video target change did not verify. The current source may not expose a video target."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("track_id"), trackId},
                       {QStringLiteral("cleared"), trackId == -1}, {QStringLiteral("verified"), true}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required}, {QStringLiteral("additionalProperties"), false}};
}

bool addEphemeralTool(VibeCutToolSurface &surface, const QString &name, const QString &description,
                      const QJsonObject &schema, const VibeCutToolSurface::Handler &handler, QString *error)
{
    VibeCutToolPolicy policy;
    policy.name = name;
    policy.risk = VibeCutToolRisk::ReadOnly;
    policy.reversible = false;
    policy.mutatesProject = false;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("description"), description},
                                            {QStringLiteral("input_schema"), schema}},
                                policy, handler, error);
}
} // namespace

bool registerVibeCutRoutingTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject statusSchema = objectSchema(
        QJsonObject{{QStringLiteral("audio_track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                                   {QStringLiteral("description"), QStringLiteral("Optional audio track id to include currently assignable source streams for.")}}}},
        QJsonArray{});
    if (!addEphemeralTool(surface, QStringLiteral("routing_status"),
                          QStringLiteral("Inspect current Kdenlive video/audio insertion targets. Optionally include the source audio streams currently assignable to one audio track. This changes no project content."),
                          statusSchema, routingStatus, error)) return false;

    const QJsonObject audioSchema = objectSchema(
        QJsonObject{{QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                    {QStringLiteral("stream"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}}},
        QJsonArray{QStringLiteral("track_id"), QStringLiteral("stream")});
    if (!addEphemeralTool(surface, QStringLiteral("audio_target_set"),
                          QStringLiteral("Assign one currently available source audio stream to an audio target track for future insert/overwrite operations. The stream must be reported by routing_status; this changes editor targeting, not existing project content."),
                          audioSchema, setAudioTarget, error)) return false;

    const QJsonObject videoSchema = objectSchema(
        QJsonObject{{QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                             {QStringLiteral("minimum"), -1},
                                                             {QStringLiteral("description"), QStringLiteral("Video track id, or -1 to clear the video insertion target.")}}}},
        QJsonArray{QStringLiteral("track_id")});
    return addEphemeralTool(surface, QStringLiteral("video_target_set"),
                            QStringLiteral("Set or clear Kdenlive's video insertion target for future edits. This changes editor targeting, not existing project content."),
                            videoSchema, setVideoTarget, error);
}
