/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutbintools.h"

#include "bin/clipcreator.hpp"
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

std::shared_ptr<TimelineItemModel> currentTimelineModel()
{
    if (!pCore || !pCore->window()) return nullptr;
    TimelineWidget *timeline = pCore->window()->getCurrentTimeline();
    return timeline ? timeline->model() : nullptr;
}

QJsonObject listBin(const QJsonObject &)
{
    if (!pCore) return err(QStringLiteral("Kdenlive core is unavailable."));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) return err(QStringLiteral("Project bin model is unavailable."));
    QJsonArray clips;
    for (const QString &binId : model->getAllClipIds()) {
        const std::shared_ptr<ProjectClip> clip = model->getClipByBinID(binId);
        if (!clip) continue;
        QString name = clip->getProducerProperty(QStringLiteral("kdenlive:clipname"));
        if (name.isEmpty() && !clip->url().isEmpty()) name = QFileInfo(clip->url()).fileName();
        clips.append(QJsonObject{{QStringLiteral("bin_id"), binId},
                                 {QStringLiteral("name"), name},
                                 {QStringLiteral("url"), clip->url()},
                                 {QStringLiteral("clip_type"), static_cast<int>(clip->clipType())},
                                 {QStringLiteral("duration_frames"), clip->getFramePlaytime()},
                                 {QStringLiteral("has_audio"), clip->hasAudio()},
                                 {QStringLiteral("has_video"), clip->hasVideo()},
                                 {QStringLiteral("timeline_instances"), clip->timelineInstances().size()}});
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clips"), clips}};
}

QJsonObject importFile(const QJsonObject &input)
{
    if (!pCore) return err(QStringLiteral("Kdenlive core is unavailable."));
    const QString requestedPath = input.value(QStringLiteral("path")).toString().trimmed();
    if (requestedPath.isEmpty()) return err(QStringLiteral("path must not be empty"));
    QFileInfo info(requestedPath);
    if (info.isRelative()) info.setFile(QFileInfo(requestedPath).absoluteFilePath());
    if (!info.exists() || !info.isFile()) return err(QStringLiteral("Local media file does not exist: %1").arg(info.absoluteFilePath()));

    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) return err(QStringLiteral("Project bin model is unavailable."));
    const QString parentFolder = input.value(QStringLiteral("parent_folder_id")).toString(QStringLiteral("-1"));
    if (parentFolder != QLatin1String("-1") && !model->getFolderByBinId(parentFolder)) {
        return err(QStringLiteral("Bin folder '%1' does not exist.").arg(parentFolder));
    }

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    const QString binId = ClipCreator::createClipFromFile(info.absoluteFilePath(), parentFolder, model, undo, redo);
    if (binId.isEmpty() || binId == QLatin1String("-1") || !model->getClipByBinID(binId)) {
        undo();
        return err(QStringLiteral("Kdenlive could not import media file: %1").arg(info.absoluteFilePath()));
    }
    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: import media"));
    const std::shared_ptr<ProjectClip> clip = model->getClipByBinID(binId);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("bin_id"), binId},
                       {QStringLiteral("path"), info.absoluteFilePath()},
                       {QStringLiteral("duration_frames"), clip ? clip->getFramePlaytime() : 0},
                       {QStringLiteral("verified"), clip != nullptr}};
}

QJsonObject insertBinClip(const QJsonObject &input)
{
    if (!pCore) return err(QStringLiteral("Kdenlive core is unavailable."));
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    const int trackId = input.value(QStringLiteral("track_id")).toInt(-1);
    const int position = input.value(QStringLiteral("position_frame")).toInt(-1);
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty"));
    if (position < 0) return err(QStringLiteral("position_frame must be >= 0"));

    const std::shared_ptr<ProjectItemModel> binModel = pCore->projectItemModel();
    if (!binModel || !binModel->getClipByBinID(binId)) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    const std::shared_ptr<TimelineItemModel> timeline = currentTimelineModel();
    if (!timeline) return err(QStringLiteral("No timeline is open."));
    if (!timeline->isTrack(trackId)) return err(QStringLiteral("Track id %1 does not exist.").arg(trackId));

    int clipId = -1;
    if (!timeline->requestClipInsertion(binId, trackId, position, clipId, true, true, true)) {
        return err(QStringLiteral("Kdenlive rejected inserting bin clip '%1' on track %2 at frame %3.")
                       .arg(binId).arg(trackId).arg(position));
    }
    if (!timeline->isClip(clipId) || timeline->getClipBinId(clipId) != binId ||
        timeline->getClipTrackId(clipId) != trackId || timeline->getClipPosition(clipId) != position) {
        return err(QStringLiteral("Bin clip insertion returned success but live timeline state did not verify."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("bin_id"), binId}, {QStringLiteral("clip_id"), clipId},
                       {QStringLiteral("track_id"), trackId}, {QStringLiteral("position_frame"), position},
                       {QStringLiteral("duration_frames"), timeline->getClipPlaytime(clipId)}, {QStringLiteral("verified"), true}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required}, {QStringLiteral("additionalProperties"), false}};
}

bool registerEdit(VibeCutToolSurface &surface, const QString &name, const QString &description, const QJsonObject &schema,
                  const VibeCutToolSurface::Handler &handler, QString *error)
{
    VibeCutToolPolicy policy;
    policy.name = name;
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("description"), description},
                                            {QStringLiteral("input_schema"), schema}}, policy, handler, error);
}
} // namespace

bool registerVibeCutBinTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    const QJsonObject listSchema{{QStringLiteral("name"), QStringLiteral("bin_list")},
                                 {QStringLiteral("description"), QStringLiteral("List project-bin media with stable bin ids, names, local URLs, producer type, duration, audio/video capability and timeline instance count. Read-only.")},
                                 {QStringLiteral("input_schema"), noArgs}};
    VibeCutToolPolicy listPolicy;
    listPolicy.name = QStringLiteral("bin_list");
    listPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(listSchema, listPolicy, listBin, error)) return false;

    const QJsonObject importInput = objectSchema(QJsonObject{
        {QStringLiteral("path"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                             {QStringLiteral("description"), QStringLiteral("Existing local media file path. Network URLs and directories are not accepted.")}}},
        {QStringLiteral("parent_folder_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}},
        QJsonArray{QStringLiteral("path")});
    if (!registerEdit(surface, QStringLiteral("bin_import_file"),
                      QStringLiteral("Import one existing local media file into Kdenlive's project bin through ClipCreator with native undo/redo. Does not fetch network content or access a shell."),
                      importInput, importFile, error)) return false;

    const QJsonObject insertInput = objectSchema(QJsonObject{
        {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
        {QStringLiteral("position_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}}},
        QJsonArray{QStringLiteral("bin_id"), QStringLiteral("track_id"), QStringLiteral("position_frame")});
    return registerEdit(surface, QStringLiteral("bin_insert_timeline"),
                        QStringLiteral("Insert an existing project-bin clip onto an exact timeline track/frame using Kdenlive's native undoable clip insertion and verify the resulting clip id/bin id/position."),
                        insertInput, insertBinClip, error);
}
