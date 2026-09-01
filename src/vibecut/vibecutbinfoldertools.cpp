/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutbinfoldertools.h"

#include "bin/abstractprojectitem.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "vibecuttoolsurface.h"

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

std::shared_ptr<ProjectFolder> folderById(const QString &id, QJsonObject &failure)
{
    if (!pCore) {
        failure = err(QStringLiteral("Kdenlive core is unavailable."));
        return nullptr;
    }
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) {
        failure = err(QStringLiteral("Project bin model is unavailable."));
        return nullptr;
    }
    const std::shared_ptr<ProjectFolder> folder = model->getFolderByBinId(id);
    if (!folder) failure = err(QStringLiteral("Bin folder '%1' does not exist.").arg(id));
    return folder;
}

QJsonObject renameFolder(const QJsonObject &input)
{
    const QString folderId = input.value(QStringLiteral("folder_id")).toString().trimmed();
    const QString name = input.value(QStringLiteral("name")).toString().trimmed();
    if (folderId.isEmpty()) return err(QStringLiteral("folder_id must not be empty."));
    if (name.isEmpty()) return err(QStringLiteral("name must not be empty."));

    QJsonObject failure;
    const std::shared_ptr<ProjectFolder> folder = folderById(folderId, failure);
    if (!folder) return failure;
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (folder == model->getRootFolder()) return err(QStringLiteral("The project-bin root folder cannot be renamed through this tool."));

    const QString oldName = folder->name();
    if (oldName == name) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("folder_id"), folderId},
                           {QStringLiteral("old_name"), oldName}, {QStringLiteral("name"), name},
                           {QStringLiteral("changed"), false}, {QStringLiteral("verified"), true}};
    }

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (!model->requestRenameFolder(folder, name, undo, redo)) {
        return err(QStringLiteral("Kdenlive rejected renaming folder '%1'.").arg(folderId));
    }
    const std::shared_ptr<ProjectFolder> live = model->getFolderByBinId(folderId);
    if (!live || live->name() != name) {
        undo();
        return err(QStringLiteral("Folder rename did not verify; operation was rolled back."));
    }
    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: rename bin folder"));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("folder_id"), folderId},
                       {QStringLiteral("old_name"), oldName}, {QStringLiteral("name"), name},
                       {QStringLiteral("changed"), true}, {QStringLiteral("verified"), true}};
}

QJsonObject deleteEmptyFolder(const QJsonObject &input)
{
    const QString folderId = input.value(QStringLiteral("folder_id")).toString().trimmed();
    if (folderId.isEmpty()) return err(QStringLiteral("folder_id must not be empty."));

    QJsonObject failure;
    const std::shared_ptr<ProjectFolder> folder = folderById(folderId, failure);
    if (!folder) return failure;
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (folder == model->getRootFolder()) return err(QStringLiteral("The project-bin root folder cannot be deleted."));
    if (folder->childCount() != 0) {
        return err(QStringLiteral("Folder '%1' is not empty. VibeCut refuses implicit recursive deletion; move/delete its contents explicitly first.").arg(folderId));
    }

    const QString oldName = folder->name();
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (!model->requestBinClipDeletion(std::static_pointer_cast<AbstractProjectItem>(folder), undo, redo)) {
        return err(QStringLiteral("Kdenlive rejected deleting empty folder '%1'.").arg(folderId));
    }
    if (model->getFolderByBinId(folderId)) {
        undo();
        return err(QStringLiteral("Folder deletion did not verify; operation was rolled back."));
    }
    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: delete empty bin folder"));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("folder_id"), folderId},
                       {QStringLiteral("name"), oldName}, {QStringLiteral("verified"), true}};
}

QJsonObject schemaFor(const QString &name, const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("name"), name},
                       {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                    {QStringLiteral("properties"), properties},
                                                                    {QStringLiteral("required"), required},
                                                                    {QStringLiteral("additionalProperties"), false}}}};
}
} // namespace

bool registerVibeCutBinFolderTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject renameSchema = schemaFor(QStringLiteral("bin_folder_rename"),
                                               QJsonObject{{QStringLiteral("folder_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                                           {QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}},
                                               QJsonArray{QStringLiteral("folder_id"), QStringLiteral("name")});
    QJsonObject renameWithDescription = renameSchema;
    renameWithDescription.insert(QStringLiteral("description"), QStringLiteral("Rename a non-root project-bin folder through Kdenlive's native undoable folder-rename path and verify the live name."));
    VibeCutToolPolicy renamePolicy;
    renamePolicy.name = QStringLiteral("bin_folder_rename");
    renamePolicy.risk = VibeCutToolRisk::ReversibleEdit;
    renamePolicy.reversible = true;
    renamePolicy.mutatesProject = true;
    if (!surface.registerTool(renameWithDescription, renamePolicy, renameFolder, error)) return false;

    const QJsonObject deleteSchema = schemaFor(QStringLiteral("bin_folder_delete_empty"),
                                               QJsonObject{{QStringLiteral("folder_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}},
                                               QJsonArray{QStringLiteral("folder_id")});
    QJsonObject deleteWithDescription = deleteSchema;
    deleteWithDescription.insert(QStringLiteral("description"), QStringLiteral("Delete a non-root project-bin folder only when it is empty, using Kdenlive's native undo accumulation. Refuses recursive/implicit content deletion and verifies the folder is gone."));
    VibeCutToolPolicy deletePolicy;
    deletePolicy.name = QStringLiteral("bin_folder_delete_empty");
    deletePolicy.risk = VibeCutToolRisk::ReversibleEdit;
    deletePolicy.reversible = true;
    deletePolicy.mutatesProject = true;
    return surface.registerTool(deleteWithDescription, deletePolicy, deleteEmptyFolder, error);
}
