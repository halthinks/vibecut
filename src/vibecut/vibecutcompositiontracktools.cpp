/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutcompositiontracktools.h"

#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelinefunctions.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

TimelineWidget *currentTimeline()
{
    if (!pCore || !pCore->window()) return nullptr;
    return pCore->window()->getCurrentTimeline();
}

QJsonObject inspect(const QJsonObject &input)
{
    const int compositionId = input.value(QStringLiteral("composition_id")).toInt(-1);
    TimelineWidget *timeline = currentTimeline();
    const std::shared_ptr<TimelineItemModel> model = timeline ? timeline->model() : nullptr;
    TimelineController *controller = timeline ? timeline->controller() : nullptr;
    if (!model || !controller) return err(QStringLiteral("No timeline is open."));
    if (!model->isComposition(compositionId)) return err(QStringLiteral("Composition id %1 does not exist.").arg(compositionId));

    const QPair<int, int> tracks = controller->getCompositionATrack(compositionId);
    const bool automatic = controller->compositionAutoTrack(compositionId);
    int aTrackId = -1;
    if (tracks.first > 0) {
        const int position = tracks.first - 1;
        if (position >= 0 && position < model->getTracksCount()) aTrackId = model->getTrackIndexFromPosition(position);
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("composition_id"), compositionId},
                       {QStringLiteral("automatic"), automatic},
                       {QStringLiteral("a_track_id"), automatic ? -1 : aTrackId},
                       {QStringLiteral("a_track_mlt_index"), tracks.first},
                       {QStringLiteral("b_track_mlt_index"), tracks.second},
                       {QStringLiteral("composition_track_id"), model->getItemTrackId(compositionId)}};
}

QJsonObject setATrack(const QJsonObject &input)
{
    const int compositionId = input.value(QStringLiteral("composition_id")).toInt(-1);
    const int requestedTrackId = input.value(QStringLiteral("a_track_id")).toInt(-1);
    TimelineWidget *timeline = currentTimeline();
    const std::shared_ptr<TimelineItemModel> model = timeline ? timeline->model() : nullptr;
    TimelineController *controller = timeline ? timeline->controller() : nullptr;
    if (!model || !controller) return err(QStringLiteral("No timeline is open."));
    if (!model->isComposition(compositionId)) return err(QStringLiteral("Composition id %1 does not exist.").arg(compositionId));

    int requestedMltIndex = -1;
    if (requestedTrackId != -1) {
        if (!model->isTrack(requestedTrackId) || model->isAudioTrack(requestedTrackId)) {
            return err(QStringLiteral("a_track_id must be -1 for automatic or an existing video track id."));
        }
        requestedMltIndex = model->getTrackMltIndex(requestedTrackId);
        if (requestedMltIndex < 1) return err(QStringLiteral("The selected video track cannot be used as a composition A-track."));
    }

    const bool oldAutomatic = controller->compositionAutoTrack(compositionId);
    const QPair<int, int> oldTracks = controller->getCompositionATrack(compositionId);
    if ((requestedTrackId == -1 && oldAutomatic) ||
        (requestedTrackId != -1 && !oldAutomatic && oldTracks.first == requestedMltIndex)) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("composition_id"), compositionId},
                           {QStringLiteral("a_track_id"), requestedTrackId}, {QStringLiteral("changed"), false},
                           {QStringLiteral("verified"), true}};
    }

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (!TimelineFunctions::setCompositionATrack(model, compositionId, requestedMltIndex, undo, redo, false)) {
        return err(QStringLiteral("Kdenlive rejected the requested composition A-track assignment."));
    }

    const bool liveAutomatic = controller->compositionAutoTrack(compositionId);
    const QPair<int, int> liveTracks = controller->getCompositionATrack(compositionId);
    const bool verified = requestedTrackId == -1 ? liveAutomatic : (!liveAutomatic && liveTracks.first == requestedMltIndex);
    if (!verified) {
        undo();
        return err(QStringLiteral("Composition A-track assignment did not verify on the live timeline."));
    }
    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: set composition A-track"));

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("composition_id"), compositionId},
                       {QStringLiteral("automatic"), liveAutomatic}, {QStringLiteral("a_track_id"), requestedTrackId},
                       {QStringLiteral("a_track_mlt_index"), liveTracks.first}, {QStringLiteral("old_automatic"), oldAutomatic},
                       {QStringLiteral("old_a_track_mlt_index"), oldTracks.first}, {QStringLiteral("changed"), true},
                       {QStringLiteral("verified"), true}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required}, {QStringLiteral("additionalProperties"), false}};
}
} // namespace

bool registerVibeCutCompositionTrackTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject inspectInput = objectSchema(
        QJsonObject{{QStringLiteral("composition_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}},
        QJsonArray{QStringLiteral("composition_id")});
    const QJsonObject inspectSchema{{QStringLiteral("name"), QStringLiteral("composition_a_track_inspect")},
                                    {QStringLiteral("description"), QStringLiteral("Inspect whether a composition uses automatic A-track selection or a forced video track. Returns stable Kdenlive track id plus underlying MLT indexes. Read-only." )},
                                    {QStringLiteral("input_schema"), inspectInput}};
    VibeCutToolPolicy inspectPolicy;
    inspectPolicy.name = QStringLiteral("composition_a_track_inspect");
    inspectPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(inspectSchema, inspectPolicy, inspect, error)) return false;

    const QJsonObject setInput = objectSchema(
        QJsonObject{{QStringLiteral("composition_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                    {QStringLiteral("a_track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), -1},
                                                               {QStringLiteral("description"), QStringLiteral("Stable Kdenlive video track id, or -1 to restore automatic lower-video-track selection.")}}}},
        QJsonArray{QStringLiteral("composition_id"), QStringLiteral("a_track_id")});
    const QJsonObject setSchema{{QStringLiteral("name"), QStringLiteral("composition_a_track_set")},
                                {QStringLiteral("description"), QStringLiteral("Set a composition's A-track using a stable Kdenlive video track id, converting internally to Kdenlive's MLT index semantics. Uses native undo accumulation and live verification; -1 restores automatic selection." )},
                                {QStringLiteral("input_schema"), setInput}};
    VibeCutToolPolicy setPolicy;
    setPolicy.name = QStringLiteral("composition_a_track_set");
    setPolicy.risk = VibeCutToolRisk::ReversibleEdit;
    setPolicy.reversible = true;
    setPolicy.mutatesProject = true;
    return surface.registerTool(setSchema, setPolicy, setATrack, error);
}
