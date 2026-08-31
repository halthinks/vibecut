/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuttransitiontools.h"

#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"
#include "transitions/transitionsrepository.hpp"
#include "vibecuttoolsurface.h"

#include <QJsonArray>

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

std::shared_ptr<TimelineItemModel> currentModel()
{
    TimelineWidget *timeline = currentTimeline();
    return timeline ? timeline->model() : nullptr;
}

bool validComposition(const std::shared_ptr<TimelineItemModel> &model, int compositionId, QJsonObject &failure)
{
    if (!model) {
        failure = err(QStringLiteral("No timeline is open."));
        return false;
    }
    if (!model->isComposition(compositionId)) {
        failure = err(QStringLiteral("Composition id %1 does not exist on the active timeline.").arg(compositionId));
        return false;
    }
    return true;
}

QJsonObject listTransitions(const QJsonObject &)
{
    QJsonArray transitions;
    for (const QPair<QString, QString> &entry : TransitionsRepository::get()->getNames()) {
        transitions.append(QJsonObject{{QStringLiteral("id"), entry.first},
                                       {QStringLiteral("name"), entry.second},
                                       {QStringLiteral("audio"), TransitionsRepository::get()->isAudio(entry.first)},
                                       {QStringLiteral("composition"), TransitionsRepository::get()->isComposition(entry.first)},
                                       {QStringLiteral("luma"), TransitionsRepository::get()->isLuma(entry.first)}});
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("transitions"), transitions}};
}

QJsonObject addTransition(const QJsonObject &input)
{
    const QString transitionId = input.value(QStringLiteral("transition_id")).toString().trimmed();
    const int trackId = input.value(QStringLiteral("track_id")).toInt(-1);
    const int position = input.value(QStringLiteral("position_frame")).toInt(-1);
    const int duration = input.contains(QStringLiteral("duration_frames")) ? input.value(QStringLiteral("duration_frames")).toInt(-1) : -1;
    if (transitionId.isEmpty()) return err(QStringLiteral("transition_id must not be empty"));
    if (position < 0) return err(QStringLiteral("position_frame must be >= 0"));
    if (duration == 0 || duration < -1) return err(QStringLiteral("duration_frames must be > 0 when provided"));
    if (!TransitionsRepository::get()->exists(transitionId)) {
        return err(QStringLiteral("Unknown Kdenlive transition id '%1'. Call transitions_list first.").arg(transitionId));
    }
    if (TransitionsRepository::get()->isAudio(transitionId)) {
        return err(QStringLiteral("Audio transitions are not yet exposed by transition_add; use a video/composition transition from transitions_list."));
    }

    TimelineWidget *timeline = currentTimeline();
    const std::shared_ptr<TimelineItemModel> model = timeline ? timeline->model() : nullptr;
    TimelineController *controller = timeline ? timeline->controller() : nullptr;
    if (!model || !controller) return err(QStringLiteral("No timeline is open."));
    if (!model->isTrack(trackId) || model->isAudioTrack(trackId)) {
        return err(QStringLiteral("track_id %1 must be an existing video track.").arg(trackId));
    }

    const int compositionId = controller->insertComposition(trackId, position, transitionId, true, duration);
    if (compositionId < 0 || !model->isComposition(compositionId)) {
        return err(QStringLiteral("Kdenlive rejected transition '%1' on track %2 at frame %3.")
                       .arg(transitionId).arg(trackId).arg(position));
    }
    if (model->getItemPosition(compositionId) != position || model->getItemTrackId(compositionId) != trackId) {
        return err(QStringLiteral("Transition insertion returned id %1 but live composition position/track did not match.").arg(compositionId));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("composition_id"), compositionId},
                       {QStringLiteral("transition_id"), transitionId}, {QStringLiteral("track_id"), trackId},
                       {QStringLiteral("position_frame"), position},
                       {QStringLiteral("duration_frames"), model->getItemPlaytime(compositionId)},
                       {QStringLiteral("verified"), true}};
}

QJsonObject moveTransition(const QJsonObject &input)
{
    const int compositionId = input.value(QStringLiteral("composition_id")).toInt(-1);
    const int position = input.value(QStringLiteral("position_frame")).toInt(-1);
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    QJsonObject failure;
    if (!validComposition(model, compositionId, failure)) return failure;
    if (position < 0) return err(QStringLiteral("position_frame must be >= 0"));
    const int trackId = input.contains(QStringLiteral("track_id")) ? input.value(QStringLiteral("track_id")).toInt(-1)
                                                                   : model->getItemTrackId(compositionId);
    if (!model->isTrack(trackId) || model->isAudioTrack(trackId)) {
        return err(QStringLiteral("track_id %1 must be an existing video track.").arg(trackId));
    }
    const int oldTrack = model->getItemTrackId(compositionId);
    const int oldPosition = model->getItemPosition(compositionId);
    if (!model->requestCompositionMove(compositionId, trackId, position, true, true, false, false)) {
        return err(QStringLiteral("Kdenlive rejected moving composition %1 to track %2 at frame %3.")
                       .arg(compositionId).arg(trackId).arg(position));
    }
    if (!model->isComposition(compositionId) || model->getItemTrackId(compositionId) != trackId || model->getItemPosition(compositionId) != position) {
        return err(QStringLiteral("Composition move returned success but live track/position did not match."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("composition_id"), compositionId},
                       {QStringLiteral("old_track_id"), oldTrack}, {QStringLiteral("old_position_frame"), oldPosition},
                       {QStringLiteral("track_id"), trackId}, {QStringLiteral("position_frame"), position},
                       {QStringLiteral("verified"), true}};
}

QJsonObject resizeTransition(const QJsonObject &input)
{
    const int compositionId = input.value(QStringLiteral("composition_id")).toInt(-1);
    const int duration = input.value(QStringLiteral("new_duration_frames")).toInt(-1);
    const QString side = input.value(QStringLiteral("side")).toString();
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    QJsonObject failure;
    if (!validComposition(model, compositionId, failure)) return failure;
    if (duration <= 0) return err(QStringLiteral("new_duration_frames must be > 0"));
    if (side != QLatin1String("start") && side != QLatin1String("end")) return err(QStringLiteral("side must be 'start' or 'end'"));
    const int oldDuration = model->getItemPlaytime(compositionId);
    const int oldPosition = model->getItemPosition(compositionId);
    const int applied = model->requestItemResize(compositionId, duration, side == QLatin1String("end"), true, -1, true);
    if (applied <= 0) return err(QStringLiteral("Kdenlive rejected resizing composition %1.").arg(compositionId));
    if (!model->isComposition(compositionId) || model->getItemPlaytime(compositionId) != applied) {
        return err(QStringLiteral("Composition resize returned %1 but live duration did not match.").arg(applied));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("composition_id"), compositionId},
                       {QStringLiteral("side"), side}, {QStringLiteral("old_duration_frames"), oldDuration},
                       {QStringLiteral("duration_frames"), applied}, {QStringLiteral("old_position_frame"), oldPosition},
                       {QStringLiteral("position_frame"), model->getItemPosition(compositionId)}, {QStringLiteral("verified"), true}};
}

QJsonObject removeTransition(const QJsonObject &input)
{
    const int compositionId = input.value(QStringLiteral("composition_id")).toInt(-1);
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    QJsonObject failure;
    if (!validComposition(model, compositionId, failure)) return failure;
    const int oldTrack = model->getItemTrackId(compositionId);
    const int oldPosition = model->getItemPosition(compositionId);
    const int oldDuration = model->getItemPlaytime(compositionId);
    if (!model->requestItemDeletion(compositionId, true)) {
        return err(QStringLiteral("Kdenlive rejected removing composition %1.").arg(compositionId));
    }
    if (model->isComposition(compositionId)) {
        return err(QStringLiteral("Composition removal returned success but id %1 is still present.").arg(compositionId));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("removed_composition_id"), compositionId},
                       {QStringLiteral("old_track_id"), oldTrack}, {QStringLiteral("old_position_frame"), oldPosition},
                       {QStringLiteral("old_duration_frames"), oldDuration}, {QStringLiteral("verified"), true}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required}, {QStringLiteral("additionalProperties"), false}};
}

bool registerEditTool(VibeCutToolSurface &surface, const QString &name, const QString &description, const QJsonObject &inputSchema,
                      const VibeCutToolSurface::Handler &handler, QString *error)
{
    VibeCutToolPolicy policy;
    policy.name = name;
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("description"), description},
                                            {QStringLiteral("input_schema"), inputSchema}},
                                policy, handler, error);
}
} // namespace

bool registerVibeCutTransitionTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    const QJsonObject listSchema{{QStringLiteral("name"), QStringLiteral("transitions_list")},
                                 {QStringLiteral("description"), QStringLiteral("List the actual transitions/compositions installed in this Kdenlive runtime with stable ids and names. Read-only; use before transition_add instead of inventing ids.")},
                                 {QStringLiteral("input_schema"), noArgs}};
    VibeCutToolPolicy listPolicy;
    listPolicy.name = QStringLiteral("transitions_list");
    listPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(listSchema, listPolicy, listTransitions, error)) return false;

    const QJsonObject addInput{{QStringLiteral("type"), QStringLiteral("object")},
                               {QStringLiteral("properties"), QJsonObject{
                                   {QStringLiteral("transition_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                   {QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                   {QStringLiteral("position_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                   {QStringLiteral("duration_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}}}},
                               {QStringLiteral("required"), QJsonArray{QStringLiteral("transition_id"), QStringLiteral("track_id"), QStringLiteral("position_frame")}},
                               {QStringLiteral("additionalProperties"), false}};
    if (!registerEditTool(surface, QStringLiteral("transition_add"),
                          QStringLiteral("Insert one installed Kdenlive video transition/composition at an exact track/frame using the native TimelineController insertion path, then verify live composition state."),
                          addInput, addTransition, error)) return false;

    const QJsonObject moveInput = objectSchema(QJsonObject{
        {QStringLiteral("composition_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
        {QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
        {QStringLiteral("position_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}}},
        QJsonArray{QStringLiteral("composition_id"), QStringLiteral("position_frame")});
    if (!registerEditTool(surface, QStringLiteral("transition_move"),
                          QStringLiteral("Move an existing composition to an exact frame and optional video track using Kdenlive's native undoable composition move, then verify live state."),
                          moveInput, moveTransition, error)) return false;

    const QJsonObject resizeInput = objectSchema(QJsonObject{
        {QStringLiteral("composition_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
        {QStringLiteral("side"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                             {QStringLiteral("enum"), QJsonArray{QStringLiteral("start"), QStringLiteral("end")}}}},
        {QStringLiteral("new_duration_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}}},
        QJsonArray{QStringLiteral("composition_id"), QStringLiteral("side"), QStringLiteral("new_duration_frames")});
    if (!registerEditTool(surface, QStringLiteral("transition_resize"),
                          QStringLiteral("Resize the start or end of an existing composition to an exact duration using Kdenlive's native item resize and verify duration/position."),
                          resizeInput, resizeTransition, error)) return false;

    const QJsonObject removeInput = objectSchema(QJsonObject{
        {QStringLiteral("composition_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}},
        QJsonArray{QStringLiteral("composition_id")});
    return registerEditTool(surface, QStringLiteral("transition_remove"),
                            QStringLiteral("Remove an existing timeline composition using Kdenlive's native undoable item deletion and verify the composition is gone."),
                            removeInput, removeTransition, error);
}
