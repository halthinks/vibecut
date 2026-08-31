/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecutedittools.h"

#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

std::shared_ptr<TimelineItemModel> currentModel()
{
    if (!pCore || !pCore->window()) return nullptr;
    TimelineWidget *timeline = pCore->window()->getCurrentTimeline();
    return timeline ? timeline->model() : nullptr;
}

bool validClip(const std::shared_ptr<TimelineItemModel> &model, int clipId, QJsonObject &failure)
{
    if (!model) {
        failure = err(QStringLiteral("No timeline is open."));
        return false;
    }
    if (!model->isClip(clipId)) {
        failure = err(QStringLiteral("Clip id %1 does not exist on the active timeline.").arg(clipId));
        return false;
    }
    return true;
}

QJsonObject moveClip(const QJsonObject &input)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    const int position = input.value(QStringLiteral("position_frame")).toInt(-1);
    std::shared_ptr<TimelineItemModel> model = currentModel();
    QJsonObject failure;
    if (!validClip(model, clipId, failure)) return failure;
    if (position < 0) return err(QStringLiteral("position_frame must be >= 0"));

    const int targetTrack = input.contains(QStringLiteral("track_id"))
                                ? input.value(QStringLiteral("track_id")).toInt(-1)
                                : model->getClipTrackId(clipId);
    if (!model->isTrack(targetTrack)) return err(QStringLiteral("Track id %1 does not exist.").arg(targetTrack));
    const int oldTrack = model->getClipTrackId(clipId);
    const int oldPosition = model->getClipPosition(clipId);
    if (!model->requestClipMove(clipId, targetTrack, position, true, true, true, true)) {
        return err(QStringLiteral("Kdenlive rejected moving clip %1 to track %2 at frame %3.").arg(clipId).arg(targetTrack).arg(position));
    }
    const bool verified = model->isClip(clipId) && model->getClipTrackId(clipId) == targetTrack && model->getClipPosition(clipId) == position;
    if (!verified) return err(QStringLiteral("Move returned success but live timeline state did not match the requested destination."));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId},
                       {QStringLiteral("old_track_id"), oldTrack}, {QStringLiteral("old_position_frame"), oldPosition},
                       {QStringLiteral("track_id"), targetTrack}, {QStringLiteral("position_frame"), position},
                       {QStringLiteral("verified"), true}};
}

QJsonObject deleteClip(const QJsonObject &input)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    std::shared_ptr<TimelineItemModel> model = currentModel();
    QJsonObject failure;
    if (!validClip(model, clipId, failure)) return failure;

    const QString name = model->getClipName(clipId);
    const int position = model->getClipPosition(clipId);
    const int trackId = model->getClipTrackId(clipId);
    if (!model->requestItemDeletion(clipId, true)) {
        return err(QStringLiteral("Kdenlive rejected deleting clip %1.").arg(clipId));
    }
    if (model->isClip(clipId)) return err(QStringLiteral("Delete returned success but clip %1 is still present on the live timeline.").arg(clipId));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("deleted_clip_id"), clipId},
                       {QStringLiteral("name"), name}, {QStringLiteral("old_track_id"), trackId},
                       {QStringLiteral("old_position_frame"), position}, {QStringLiteral("verified"), true}};
}

QJsonObject trimClip(const QJsonObject &input, bool ripple)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    const int requestedDuration = input.value(QStringLiteral("new_duration_frames")).toInt(-1);
    const QString side = input.value(QStringLiteral("side")).toString();
    std::shared_ptr<TimelineItemModel> model = currentModel();
    QJsonObject failure;
    if (!validClip(model, clipId, failure)) return failure;
    if (requestedDuration <= 0) return err(QStringLiteral("new_duration_frames must be > 0"));
    if (side != QLatin1String("start") && side != QLatin1String("end")) return err(QStringLiteral("side must be 'start' or 'end'"));

    const int oldDuration = model->getClipPlaytime(clipId);
    const int oldPosition = model->getClipPosition(clipId);
    const bool right = side == QLatin1String("end");
    int applied = -1;
    if (ripple) {
        applied = model->requestItemRippleResize(model, clipId, requestedDuration, right, true, false, -1, false);
    } else {
        applied = model->requestItemResize(clipId, requestedDuration, right, true, -1, false);
    }
    if (applied <= 0) return err(QStringLiteral("Kdenlive rejected the requested %1 trim for clip %2.").arg(ripple ? QStringLiteral("ripple") : QStringLiteral("standard")).arg(clipId));

    const int liveDuration = model->getClipPlaytime(clipId);
    if (!model->isClip(clipId) || liveDuration != applied) {
        return err(QStringLiteral("Trim returned size %1 but live clip duration is %2; refusing to claim success.").arg(applied).arg(liveDuration));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId},
                       {QStringLiteral("side"), side}, {QStringLiteral("ripple"), ripple},
                       {QStringLiteral("old_duration_frames"), oldDuration}, {QStringLiteral("duration_frames"), liveDuration},
                       {QStringLiteral("old_position_frame"), oldPosition}, {QStringLiteral("position_frame"), model->getClipPosition(clipId)},
                       {QStringLiteral("verified"), true}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required}, {QStringLiteral("additionalProperties"), false}};
}

bool add(VibeCutToolSurface &surface, const QString &name, const QString &description, const QJsonObject &inputSchema,
         VibeCutToolRisk risk, const VibeCutToolSurface::Handler &handler, QString *error)
{
    VibeCutToolPolicy policy;
    policy.name = name;
    policy.risk = risk;
    policy.reversible = true;
    policy.mutatesProject = true;
    const QJsonObject schema{{QStringLiteral("name"), name}, {QStringLiteral("description"), description},
                             {QStringLiteral("input_schema"), inputSchema}};
    return surface.registerTool(schema, policy, handler, error);
}
} // namespace

bool registerVibeCutEditTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject clipId{{QStringLiteral("type"), QStringLiteral("integer")},
                             {QStringLiteral("description"), QStringLiteral("Stable timeline clip id from timeline_list_clips.")}};

    if (!add(surface, QStringLiteral("clip_move"),
             QStringLiteral("Move a timeline clip to an exact frame and optional track using Kdenlive's undoable TimelineModel API, then verify live position/track state."),
             objectSchema(QJsonObject{{QStringLiteral("clip_id"), clipId},
                                      {QStringLiteral("position_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                      {QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}},
                          QJsonArray{QStringLiteral("clip_id"), QStringLiteral("position_frame")}),
             VibeCutToolRisk::ReversibleEdit, moveClip, error)) return false;

    if (!add(surface, QStringLiteral("clip_trim"),
             QStringLiteral("Trim the start or end of a clip to an exact new duration in frames. Uses Kdenlive's undoable resize request and verifies the live duration."),
             objectSchema(QJsonObject{{QStringLiteral("clip_id"), clipId},
                                      {QStringLiteral("side"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                          {QStringLiteral("enum"), QJsonArray{QStringLiteral("start"), QStringLiteral("end")}}}},
                                      {QStringLiteral("new_duration_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}}},
                          QJsonArray{QStringLiteral("clip_id"), QStringLiteral("side"), QStringLiteral("new_duration_frames")}),
             VibeCutToolRisk::ReversibleEdit, [](const QJsonObject &input) { return trimClip(input, false); }, error)) return false;

    if (!add(surface, QStringLiteral("clip_ripple_trim"),
             QStringLiteral("Ripple-trim a clip start/end to an exact new duration, allowing Kdenlive to move following material according to its native ripple semantics. Undoable and verified."),
             objectSchema(QJsonObject{{QStringLiteral("clip_id"), clipId},
                                      {QStringLiteral("side"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                          {QStringLiteral("enum"), QJsonArray{QStringLiteral("start"), QStringLiteral("end")}}}},
                                      {QStringLiteral("new_duration_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}}},
                          QJsonArray{QStringLiteral("clip_id"), QStringLiteral("side"), QStringLiteral("new_duration_frames")}),
             VibeCutToolRisk::MajorEdit, [](const QJsonObject &input) { return trimClip(input, true); }, error)) return false;

    return add(surface, QStringLiteral("clip_delete"),
               QStringLiteral("Delete a timeline clip using Kdenlive's native undoable deletion request and verify the clip is gone. This is reversible with Undo but treated as a major edit for trust policy."),
               objectSchema(QJsonObject{{QStringLiteral("clip_id"), clipId}}, QJsonArray{QStringLiteral("clip_id")}),
               VibeCutToolRisk::MajorEdit, deleteClip, error);
}
