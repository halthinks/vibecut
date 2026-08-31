/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuttracktools.h"

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

bool locked(const std::shared_ptr<TimelineItemModel> &model, int trackId)
{
    return model && model->isTrack(trackId) && model->data(model->makeTrackIndexFromID(trackId), TimelineModel::IsLockedRole).toBool();
}

QJsonObject listTracks(const QJsonObject &)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    QJsonArray tracks;
    for (int trackId : model->getAllTracksIds()) {
        tracks.append(QJsonObject{{QStringLiteral("track_id"), trackId},
                                  {QStringLiteral("name"), model->getTrackFullName(trackId)},
                                  {QStringLiteral("position"), model->getTrackPosition(trackId)},
                                  {QStringLiteral("audio"), model->isAudioTrack(trackId)},
                                  {QStringLiteral("locked"), locked(model, trackId)},
                                  {QStringLiteral("clip_count"), model->getTrackClipsCount(trackId)},
                                  {QStringLiteral("composition_count"), model->getTrackCompositionsCount(trackId)}});
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("tracks"), tracks}};
}

QJsonObject createTrack(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    const int position = input.contains(QStringLiteral("position")) ? input.value(QStringLiteral("position")).toInt(-1) : -1;
    const QString name = input.value(QStringLiteral("name")).toString();
    const bool audio = input.value(QStringLiteral("audio")).toBool(false);
    if (position < -1) return err(QStringLiteral("position must be -1 or >= 0"));
    int trackId = -1;
    if (!model->requestTrackInsertion(position, trackId, name, audio)) {
        return err(QStringLiteral("Kdenlive rejected creating the requested %1 track.").arg(audio ? QStringLiteral("audio") : QStringLiteral("video")));
    }
    if (trackId < 0 || !model->isTrack(trackId) || model->isAudioTrack(trackId) != audio) {
        return err(QStringLiteral("Track creation returned success but live track type/id could not be verified."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("track_id"), trackId},
                       {QStringLiteral("name"), model->getTrackFullName(trackId)},
                       {QStringLiteral("position"), model->getTrackPosition(trackId)},
                       {QStringLiteral("audio"), audio}, {QStringLiteral("verified"), true}};
}

QJsonObject moveTrack(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    const int trackId = input.value(QStringLiteral("track_id")).toInt(-1);
    const QString direction = input.value(QStringLiteral("direction")).toString();
    if (!model->isTrack(trackId)) return err(QStringLiteral("Track id %1 does not exist.").arg(trackId));
    if (direction != QLatin1String("up") && direction != QLatin1String("down")) return err(QStringLiteral("direction must be 'up' or 'down'"));
    const int oldPosition = model->getTrackPosition(trackId);
    if (!model->requestTrackMove(model, trackId, direction == QLatin1String("up"), true)) {
        return err(QStringLiteral("Kdenlive rejected moving track %1 %2.").arg(trackId, direction));
    }
    const int newPosition = model->getTrackPosition(trackId);
    if (!model->isTrack(trackId) || newPosition == oldPosition) {
        return err(QStringLiteral("Track move returned success but live track position did not change."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("track_id"), trackId},
                       {QStringLiteral("direction"), direction}, {QStringLiteral("old_position"), oldPosition},
                       {QStringLiteral("position"), newPosition}, {QStringLiteral("verified"), true}};
}

QJsonObject setTrackLock(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    const int trackId = input.value(QStringLiteral("track_id")).toInt(-1);
    const bool desired = input.value(QStringLiteral("locked")).toBool();
    if (!model->isTrack(trackId)) return err(QStringLiteral("Track id %1 does not exist.").arg(trackId));
    const bool old = locked(model, trackId);
    if (old == desired) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("track_id"), trackId},
                           {QStringLiteral("locked"), desired}, {QStringLiteral("changed"), false}, {QStringLiteral("verified"), true}};
    }
    model->setTrackLockedState(trackId, desired);
    if (locked(model, trackId) != desired) {
        return err(QStringLiteral("Track lock change did not verify on the live timeline."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("track_id"), trackId},
                       {QStringLiteral("old_locked"), old}, {QStringLiteral("locked"), desired},
                       {QStringLiteral("changed"), true}, {QStringLiteral("verified"), true}};
}

QJsonObject deleteTrack(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    const int trackId = input.value(QStringLiteral("track_id")).toInt(-1);
    if (!model->isTrack(trackId)) return err(QStringLiteral("Track id %1 does not exist.").arg(trackId));
    const QString name = model->getTrackFullName(trackId);
    const bool audio = model->isAudioTrack(trackId);
    const int clips = model->getTrackClipsCount(trackId);
    const int compositions = model->getTrackCompositionsCount(trackId);
    if (!model->requestTrackDeletion(trackId)) {
        return err(QStringLiteral("Kdenlive rejected deleting track %1.").arg(trackId));
    }
    if (model->isTrack(trackId)) {
        return err(QStringLiteral("Track deletion returned success but track %1 is still present.").arg(trackId));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("deleted_track_id"), trackId},
                       {QStringLiteral("name"), name}, {QStringLiteral("audio"), audio},
                       {QStringLiteral("deleted_clip_count"), clips}, {QStringLiteral("deleted_composition_count"), compositions},
                       {QStringLiteral("verified"), true}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required}, {QStringLiteral("additionalProperties"), false}};
}

bool addTool(VibeCutToolSurface &surface, const QString &name, const QString &description, const QJsonObject &schema,
             VibeCutToolRisk risk, const VibeCutToolSurface::Handler &handler, QString *error)
{
    VibeCutToolPolicy policy;
    policy.name = name;
    policy.risk = risk;
    policy.reversible = true;
    policy.mutatesProject = true;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("description"), description},
                                            {QStringLiteral("input_schema"), schema}},
                                policy, handler, error);
}
} // namespace

bool registerVibeCutTrackTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    const QJsonObject listSchema{{QStringLiteral("name"), QStringLiteral("tracks_list")},
                                 {QStringLiteral("description"), QStringLiteral("List active timeline tracks with stable ids, names, order, audio/video type, lock state, clip count and composition count. Read-only; use before track edits instead of guessing ids/order.")},
                                 {QStringLiteral("input_schema"), noArgs}};
    VibeCutToolPolicy listPolicy;
    listPolicy.name = QStringLiteral("tracks_list");
    listPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(listSchema, listPolicy, listTracks, error)) return false;

    const QJsonObject createInput = objectSchema(QJsonObject{
        {QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("audio"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
        {QStringLiteral("position"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), -1}}}},
        QJsonArray{});
    if (!addTool(surface, QStringLiteral("track_create"),
                 QStringLiteral("Create an undoable audio or video track at an optional timeline position and verify the resulting live track id/type."),
                 createInput, VibeCutToolRisk::ReversibleEdit, createTrack, error)) return false;

    const QJsonObject moveInput = objectSchema(QJsonObject{
        {QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
        {QStringLiteral("direction"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("up"), QStringLiteral("down")}}}}},
        QJsonArray{QStringLiteral("track_id"), QStringLiteral("direction")});
    if (!addTool(surface, QStringLiteral("track_move"),
                 QStringLiteral("Move an existing track one step up/down among tracks of the same type using Kdenlive's native undoable track move, then verify its order changed."),
                 moveInput, VibeCutToolRisk::ReversibleEdit, moveTrack, error)) return false;

    const QJsonObject lockInput = objectSchema(QJsonObject{
        {QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
        {QStringLiteral("locked"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}},
        QJsonArray{QStringLiteral("track_id"), QStringLiteral("locked")});
    if (!addTool(surface, QStringLiteral("track_set_locked"),
                 QStringLiteral("Lock or unlock a track through Kdenlive's native setTrackLockedState path, which creates its own undo/redo command; verify the live IsLocked state afterward."),
                 lockInput, VibeCutToolRisk::ReversibleEdit, setTrackLock, error)) return false;

    const QJsonObject deleteInput = objectSchema(QJsonObject{
        {QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}},
        QJsonArray{QStringLiteral("track_id")});
    return addTool(surface, QStringLiteral("track_delete"),
                   QStringLiteral("Delete an entire track using Kdenlive's native undoable track deletion. This also deletes clips/compositions on that track, so it is governed as a major edit and reports affected counts."),
                   deleteInput, VibeCutToolRisk::MajorEdit, deleteTrack, error);
}
