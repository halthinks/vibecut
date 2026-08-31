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

    const QJsonObject inputSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{
                                      {QStringLiteral("transition_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                      {QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                      {QStringLiteral("position_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                      {QStringLiteral("duration_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("transition_id"), QStringLiteral("track_id"), QStringLiteral("position_frame")}},
                                  {QStringLiteral("additionalProperties"), false}};
    const QJsonObject addSchema{{QStringLiteral("name"), QStringLiteral("transition_add")},
                                {QStringLiteral("description"), QStringLiteral("Insert one installed Kdenlive video transition/composition at an exact track/frame using the native TimelineController insertion path, then verify live composition state.")},
                                {QStringLiteral("input_schema"), inputSchema}};
    VibeCutToolPolicy addPolicy;
    addPolicy.name = QStringLiteral("transition_add");
    addPolicy.risk = VibeCutToolRisk::ReversibleEdit;
    addPolicy.reversible = true;
    addPolicy.mutatesProject = true;
    return surface.registerTool(addSchema, addPolicy, addTransition, error);
}
