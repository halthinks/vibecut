/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecutedittools.h"

#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelinefunctions.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/model/trackmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <KLocalizedString>
#include <QJsonArray>
#include <QPoint>
#include <QSet>

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

QVector<int> resolveTracks(const std::shared_ptr<TimelineItemModel> &model, const QVector<int> &requested, QString *error)
{
    QVector<int> tracks = requested.isEmpty() ? model->getAllTracksIds() : requested;
    QSet<int> seen;
    for (int trackId : std::as_const(tracks)) {
        if (seen.contains(trackId)) {
            if (error) *error = QStringLiteral("track_id %1 appears more than once.").arg(trackId);
            return {};
        }
        seen.insert(trackId);
        if (!model->isTrack(trackId)) {
            if (error) *error = QStringLiteral("Track id %1 does not exist.").arg(trackId);
            return {};
        }
        const auto track = model->getTrackById_const(trackId);
        if (!track || track->isLocked()) {
            if (error) *error = QStringLiteral("Track id %1 is locked; range removal refuses partial/desynchronized mutation.").arg(trackId);
            return {};
        }
    }
    if (tracks.isEmpty() && error) *error = QStringLiteral("The active timeline has no editable tracks.");
    return tracks;
}

QVector<QPair<int, int>> downstreamAnchors(const std::shared_ptr<TimelineItemModel> &model, const QVector<int> &tracks, int endFrame)
{
    QVector<QPair<int, int>> anchors;
    for (int trackId : tracks) {
        int bestId = -1;
        int bestPosition = -1;
        for (int itemId : model->getItemsInRange(trackId, endFrame, -1, false)) {
            if (!model->isClip(itemId)) continue;
            const int position = model->getItemPosition(itemId);
            if (position < endFrame) continue;
            if (bestId < 0 || position < bestPosition) {
                bestId = itemId;
                bestPosition = position;
            }
        }
        if (bestId >= 0) anchors.append(qMakePair(bestId, bestPosition));
    }
    return anchors;
}

bool zoneHasItems(const std::shared_ptr<TimelineItemModel> &model, const QVector<int> &tracks, int startFrame, int endFrame)
{
    const int queryEnd = qMax(startFrame, endFrame - 1);
    for (int trackId : tracks) {
        if (!model->getItemsInRange(trackId, startFrame, queryEnd, false).isEmpty()) return true;
    }
    return false;
}

QJsonObject moveClip(const QJsonObject &input)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    const int position = input.value(QStringLiteral("position_frame")).toInt(-1);
    std::shared_ptr<TimelineItemModel> model = currentModel();
    QJsonObject failure;
    if (!validClip(model, clipId, failure)) return failure;
    if (position < 0) return err(QStringLiteral("position_frame must be >= 0"));

    const int targetTrack = input.contains(QStringLiteral("track_id")) ? input.value(QStringLiteral("track_id")).toInt(-1)
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

QJsonObject splitClip(const QJsonObject &input)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    const int frame = input.value(QStringLiteral("frame")).toInt(-1);
    std::shared_ptr<TimelineItemModel> model = currentModel();
    QJsonObject failure;
    if (!validClip(model, clipId, failure)) return failure;

    const int start = model->getClipPosition(clipId);
    const int duration = model->getClipPlaytime(clipId);
    const int end = start + duration;
    if (frame <= start || frame >= end) {
        return err(QStringLiteral("Split frame %1 must be strictly inside clip %2's range [%3,%4).")
                       .arg(frame).arg(clipId).arg(start).arg(end));
    }
    const int trackId = model->getClipTrackId(clipId);
    const QString binId = model->getClipBinId(clipId);
    if (!TimelineFunctions::requestClipCut(model, clipId, frame)) {
        return err(QStringLiteral("Kdenlive rejected splitting clip %1 at frame %2.").arg(clipId).arg(frame));
    }

    const int rightId = model->getClipByStartPosition(trackId, frame);
    const bool leftVerified = model->isClip(clipId) && model->getClipPosition(clipId) == start &&
                              model->getClipPosition(clipId) + model->getClipPlaytime(clipId) == frame;
    const bool rightVerified = rightId != -1 && model->isClip(rightId) && model->getClipPosition(rightId) == frame &&
                               model->getClipBinId(rightId) == binId;
    if (!leftVerified || !rightVerified) {
        return err(QStringLiteral("Split returned success but the two live timeline sides could not be verified."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("left_clip_id"), clipId},
                       {QStringLiteral("right_clip_id"), rightId}, {QStringLiteral("frame"), frame},
                       {QStringLiteral("track_id"), trackId}, {QStringLiteral("bin_id"), binId},
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
    if (!model->requestItemDeletion(clipId, true)) return err(QStringLiteral("Kdenlive rejected deleting clip %1.").arg(clipId));
    if (model->isClip(clipId)) return err(QStringLiteral("Delete returned success but clip %1 is still present on the live timeline.").arg(clipId));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("deleted_clip_id"), clipId}, {QStringLiteral("name"), name},
                       {QStringLiteral("old_track_id"), trackId}, {QStringLiteral("old_position_frame"), position},
                       {QStringLiteral("verified"), true}};
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
    int applied = ripple ? model->requestItemRippleResize(model, clipId, requestedDuration, right, true, false, -1, false)
                         : model->requestItemResize(clipId, requestedDuration, right, true, -1, false);
    if (applied <= 0) return err(QStringLiteral("Kdenlive rejected the requested %1 trim for clip %2.")
                                     .arg(ripple ? QStringLiteral("ripple") : QStringLiteral("standard")).arg(clipId));
    const int liveDuration = model->getClipPlaytime(clipId);
    if (!model->isClip(clipId) || liveDuration != applied) {
        return err(QStringLiteral("Trim returned size %1 but live clip duration is %2; refusing to claim success.").arg(applied).arg(liveDuration));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId}, {QStringLiteral("side"), side},
                       {QStringLiteral("ripple"), ripple}, {QStringLiteral("old_duration_frames"), oldDuration},
                       {QStringLiteral("duration_frames"), liveDuration}, {QStringLiteral("old_position_frame"), oldPosition},
                       {QStringLiteral("position_frame"), model->getClipPosition(clipId)}, {QStringLiteral("verified"), true}};
}

QJsonObject removeTimelineRange(const QJsonObject &input)
{
    const int start = input.value(QStringLiteral("start_frame")).toInt(-1);
    const int end = input.value(QStringLiteral("end_frame")).toInt(-1);
    const QString mode = input.value(QStringLiteral("mode")).toString();
    if (start < 0 || end <= start) return err(QStringLiteral("timeline_range_remove requires 0 <= start_frame < end_frame."));
    if (mode != QLatin1String("lift") && mode != QLatin1String("ripple")) return err(QStringLiteral("mode must be 'lift' or 'ripple'."));

    QVector<int> requestedTracks;
    for (const QJsonValue &value : input.value(QStringLiteral("track_ids")).toArray()) requestedTracks.append(value.toInt(-1));

    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QJsonObject verification;
    QString failure;
    if (!appendVibeCutTimelineRangeRemove(model, start, end, mode == QLatin1String("lift"), requestedTracks, undo, redo, &verification, &failure)) {
        undo();
        return err(failure.isEmpty() ? QStringLiteral("Kdenlive rejected timeline range removal.") : failure);
    }
    pCore->pushUndo(undo, redo, mode == QLatin1String("lift") ? i18n("VibeCut lift timeline range") : i18n("VibeCut ripple-remove timeline range"));
    verification.insert(QStringLiteral("ok"), true);
    verification.insert(QStringLiteral("mode"), mode);
    verification.insert(QStringLiteral("start_frame"), start);
    verification.insert(QStringLiteral("end_frame"), end);
    verification.insert(QStringLiteral("removed_frames"), end - start);
    return verification;
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
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("description"), description},
                                            {QStringLiteral("input_schema"), inputSchema}},
                                policy, handler, error);
}
} // namespace

bool appendVibeCutTimelineRangeRemove(const std::shared_ptr<TimelineItemModel> &model, int startFrame, int endFrame, bool liftOnly,
                                      const QVector<int> &trackIds, Fun &undo, Fun &redo, QJsonObject *verification, QString *error)
{
    if (error) error->clear();
    if (!model) {
        if (error) *error = QStringLiteral("No timeline is open.");
        return false;
    }
    if (startFrame < 0 || endFrame <= startFrame) {
        if (error) *error = QStringLiteral("Range removal requires 0 <= start_frame < end_frame.");
        return false;
    }
    const int beforeDuration = model->duration();
    if (beforeDuration <= 0 || endFrame > beforeDuration) {
        if (error) *error = QStringLiteral("Range [%1,%2) exceeds the live timeline duration %3.").arg(startFrame).arg(endFrame).arg(beforeDuration);
        return false;
    }

    QString trackError;
    const QVector<int> tracks = resolveTracks(model, trackIds, &trackError);
    if (tracks.isEmpty()) {
        if (error) *error = trackError;
        return false;
    }

    const QVector<QPair<int, int>> anchors = downstreamAnchors(model, tracks, endFrame);
    if (!TimelineFunctions::extractZoneWithUndo(model, tracks, QPoint(startFrame, endFrame), liftOnly, -1, {}, undo, redo)) {
        if (error) *error = QStringLiteral("Kdenlive rejected %1 of timeline range [%2,%3).")
                                .arg(liftOnly ? QStringLiteral("lifting") : QStringLiteral("ripple extraction"))
                                .arg(startFrame).arg(endFrame);
        return false;
    }

    bool verified = false;
    if (liftOnly) {
        verified = !zoneHasItems(model, tracks, startFrame, endFrame);
    } else {
        const int width = endFrame - startFrame;
        bool checkedAnchor = false;
        bool anchorsShifted = true;
        for (const auto &anchor : anchors) {
            if (!model->isClip(anchor.first)) continue;
            checkedAnchor = true;
            if (model->getItemPosition(anchor.first) != anchor.second - width) {
                anchorsShifted = false;
                break;
            }
        }
        verified = checkedAnchor ? anchorsShifted : !zoneHasItems(model, tracks, startFrame, endFrame);
    }

    if (!verified) {
        if (error) *error = QStringLiteral("Kdenlive returned success but the live timeline did not satisfy the %1 postcondition for [%2,%3); refusing to claim success.")
                                .arg(liftOnly ? QStringLiteral("lift") : QStringLiteral("ripple"))
                                .arg(startFrame).arg(endFrame);
        return false;
    }

    if (verification) {
        *verification = QJsonObject{{QStringLiteral("verified"), true},
                                    {QStringLiteral("track_count"), tracks.size()},
                                    {QStringLiteral("duration_before_frames"), beforeDuration},
                                    {QStringLiteral("duration_after_frames"), model->duration()},
                                    {QStringLiteral("downstream_anchor_count"), anchors.size()}};
    }
    return true;
}

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
    if (!add(surface, QStringLiteral("clip_split"),
             QStringLiteral("Split a clip at an exact timeline frame using Kdenlive's grouped, undoable razor operation. Verifies both resulting clip sides before success."),
             objectSchema(QJsonObject{{QStringLiteral("clip_id"), clipId},
                                      {QStringLiteral("frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}}},
                          QJsonArray{QStringLiteral("clip_id"), QStringLiteral("frame")}),
             VibeCutToolRisk::ReversibleEdit, splitClip, error)) return false;
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
    if (!add(surface, QStringLiteral("timeline_range_remove"),
             QStringLiteral("Remove an exact timeline range through Kdenlive's native accumulated zone-extraction API. Requires explicit lift or ripple semantics, fails closed on locked tracks, verifies the live postcondition, and records one native Undo step."),
             objectSchema(QJsonObject{{QStringLiteral("start_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                      {QStringLiteral("end_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}},
                                      {QStringLiteral("mode"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                          {QStringLiteral("enum"), QJsonArray{QStringLiteral("lift"), QStringLiteral("ripple")}}}},
                                      {QStringLiteral("track_ids"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                                {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                                                                {QStringLiteral("uniqueItems"), true}}}},
                          QJsonArray{QStringLiteral("start_frame"), QStringLiteral("end_frame"), QStringLiteral("mode")}),
             VibeCutToolRisk::MajorEdit, removeTimelineRange, error)) return false;
    return add(surface, QStringLiteral("clip_delete"),
               QStringLiteral("Delete a timeline clip using Kdenlive's native undoable deletion request and verify the clip is gone. Reversible with Undo but treated as a major edit for trust policy."),
               objectSchema(QJsonObject{{QStringLiteral("clip_id"), clipId}}, QJsonArray{QStringLiteral("clip_id")}),
               VibeCutToolRisk::MajorEdit, deleteClip, error);
}
