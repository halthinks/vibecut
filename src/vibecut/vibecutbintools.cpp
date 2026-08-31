/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutbintools.h"

#include "bin/bin.h"
#include "bin/clipcreator.hpp"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
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

QJsonObject listFolders(const QJsonObject &)
{
    if (!pCore) return err(QStringLiteral("Kdenlive core is unavailable."));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) return err(QStringLiteral("Project bin model is unavailable."));
    QJsonArray folders;
    for (const std::shared_ptr<ProjectFolder> &folder : model->getFolders()) {
        if (!folder) continue;
        const std::shared_ptr<AbstractProjectItem> parent = folder->parent();
        folders.append(QJsonObject{{QStringLiteral("folder_id"), folder->clipId()},
                                   {QStringLiteral("name"), folder->name()},
                                   {QStringLiteral("parent_folder_id"), parent ? parent->clipId() : QStringLiteral("-1")},
                                   {QStringLiteral("has_clips"), folder->hasChildClips()}});
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("folders"), folders}};
}

QJsonObject createFolder(const QJsonObject &input)
{
    if (!pCore) return err(QStringLiteral("Kdenlive core is unavailable."));
    const QString name = input.value(QStringLiteral("name")).toString().trimmed();
    const QString parentId = input.value(QStringLiteral("parent_folder_id")).toString(QStringLiteral("-1"));
    if (name.isEmpty()) return err(QStringLiteral("name must not be empty"));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) return err(QStringLiteral("Project bin model is unavailable."));
    if (parentId != QLatin1String("-1") && !model->getFolderByBinId(parentId)) {
        return err(QStringLiteral("Parent bin folder '%1' does not exist.").arg(parentId));
    }

    QString folderId;
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (!model->requestAddFolder(folderId, name, parentId, undo, redo) || folderId.isEmpty()) {
        undo();
        return err(QStringLiteral("Kdenlive rejected creating bin folder '%1'.").arg(name));
    }
    const std::shared_ptr<ProjectFolder> folder = model->getFolderByBinId(folderId);
    if (!folder || folder->name() != name) {
        undo();
        return err(QStringLiteral("Bin folder creation returned success but live folder state did not verify."));
    }
    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: create bin folder"));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("folder_id"), folderId},
                       {QStringLiteral("name"), folder->name()}, {QStringLiteral("parent_folder_id"), parentId},
                       {QStringLiteral("verified"), true}};
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

QJsonObject replaceSource(const QJsonObject &input)
{
    if (!pCore || !pCore->bin()) return err(QStringLiteral("Kdenlive project bin is unavailable."));
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    const QString requestedPath = input.value(QStringLiteral("path")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty"));
    if (requestedPath.isEmpty()) return err(QStringLiteral("path must not be empty"));

    QFileInfo info(requestedPath);
    if (info.isRelative()) info.setFile(QFileInfo(requestedPath).absoluteFilePath());
    if (!info.exists() || !info.isFile()) return err(QStringLiteral("Replacement media file does not exist: %1").arg(info.absoluteFilePath()));

    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) return err(QStringLiteral("Project bin model is unavailable."));
    const std::shared_ptr<ProjectClip> beforeClip = model->getClipByBinID(binId);
    if (!beforeClip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    const QString oldPath = QFileInfo(beforeClip->url()).absoluteFilePath();
    const QString newPath = info.absoluteFilePath();
    if (oldPath == newPath) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("bin_id"), binId},
                           {QStringLiteral("old_path"), oldPath}, {QStringLiteral("path"), newPath},
                           {QStringLiteral("changed"), false}, {QStringLiteral("verified"), true}};
    }
    const int instanceCount = beforeClip->timelineInstances().size();

    pCore->bin()->replaceSingleClip(binId, newPath);
    const std::shared_ptr<ProjectClip> afterClip = model->getClipByBinID(binId);
    if (!afterClip || QFileInfo(afterClip->url()).absoluteFilePath() != newPath) {
        return err(QStringLiteral("Kdenlive did not verify the replacement source on bin clip '%1'.").arg(binId));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("bin_id"), binId},
                       {QStringLiteral("old_path"), oldPath}, {QStringLiteral("path"), newPath},
                       {QStringLiteral("timeline_instances_affected"), instanceCount},
                       {QStringLiteral("changed"), true}, {QStringLiteral("verified"), true}};
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

bool registerMutation(VibeCutToolSurface &surface, const QString &name, const QString &description, const QJsonObject &schema,
                      VibeCutToolRisk risk, const VibeCutToolSurface::Handler &handler, QString *error)
{
    VibeCutToolPolicy policy;
    policy.name = name;
    policy.risk = risk;
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

    const QJsonObject foldersSchema{{QStringLiteral("name"), QStringLiteral("bin_folders_list")},
                                    {QStringLiteral("description"), QStringLiteral("List project-bin folders with stable ids, names, parent folder ids and whether they contain clips. Read-only." )},
                                    {QStringLiteral("input_schema"), noArgs}};
    VibeCutToolPolicy foldersPolicy;
    foldersPolicy.name = QStringLiteral("bin_folders_list");
    foldersPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(foldersSchema, foldersPolicy, listFolders, error)) return false;

    const QJsonObject folderCreateInput = objectSchema(QJsonObject{
        {QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("parent_folder_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                         {QStringLiteral("description"), QStringLiteral("Optional parent folder id; omit or use -1 for the bin root.")}}}},
        QJsonArray{QStringLiteral("name")});
    if (!registerMutation(surface, QStringLiteral("bin_folder_create"),
                          QStringLiteral("Create a project-bin folder through ProjectItemModel::requestAddFolder with native undo/redo and verify the resulting folder id/name."),
                          folderCreateInput, VibeCutToolRisk::ReversibleEdit, createFolder, error)) return false;

    const QJsonObject importInput = objectSchema(QJsonObject{
        {QStringLiteral("path"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                             {QStringLiteral("description"), QStringLiteral("Existing local media file path. Network URLs and directories are not accepted.")}}},
        {QStringLiteral("parent_folder_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}},
        QJsonArray{QStringLiteral("path")});
    if (!registerMutation(surface, QStringLiteral("bin_import_file"),
                          QStringLiteral("Import one existing local media file into Kdenlive's project bin through ClipCreator with native undo/redo. Does not fetch network content or access a shell."),
                          importInput, VibeCutToolRisk::ReversibleEdit, importFile, error)) return false;

    const QJsonObject replaceInput = objectSchema(QJsonObject{
        {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("path"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                             {QStringLiteral("description"), QStringLiteral("Existing local replacement media path. Kdenlive replaces the source for this bin asset and all timeline instances using its native undoable EditClipCommand path.")}}}},
        QJsonArray{QStringLiteral("bin_id"), QStringLiteral("path")});
    if (!registerMutation(surface, QStringLiteral("bin_replace_source"),
                          QStringLiteral("Replace the local source file behind an existing project-bin clip through Kdenlive's native replaceSingleClip/EditClipCommand path. All timeline instances of that bin asset follow the replacement, so this is governed as a major edit and remains undoable."),
                          replaceInput, VibeCutToolRisk::MajorEdit, replaceSource, error)) return false;

    const QJsonObject insertInput = objectSchema(QJsonObject{
        {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
        {QStringLiteral("position_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}}},
        QJsonArray{QStringLiteral("bin_id"), QStringLiteral("track_id"), QStringLiteral("position_frame")});
    return registerMutation(surface, QStringLiteral("bin_insert_timeline"),
                            QStringLiteral("Insert an existing project-bin clip onto an exact timeline track/frame using Kdenlive's native undoable clip insertion and verify the resulting clip id/bin id/position."),
                            insertInput, VibeCutToolRisk::ReversibleEdit, insertBinClip, error);
}
