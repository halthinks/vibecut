/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutrelinktools.h"

#include "bin/bin.h"
#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "vibecuttoolsurface.h"

#include <QFileInfo>
#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QJsonObject relinkMissing(const QJsonObject &input)
{
    if (!pCore || !pCore->bin()) return err(QStringLiteral("Kdenlive project bin is unavailable."));
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    const QString requestedPath = input.value(QStringLiteral("path")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty"));
    if (requestedPath.isEmpty()) return err(QStringLiteral("path must not be empty"));

    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) return err(QStringLiteral("Project bin model is unavailable."));
    const std::shared_ptr<ProjectClip> clip = model->getClipByBinID(binId);
    if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    if (!clip->hasUrl()) return err(QStringLiteral("Bin clip '%1' is generated/non-file-backed and cannot be relinked.").arg(binId));

    const FileStatus::ClipStatus status = clip->clipStatus();
    const bool sourceMissing = !clip->sourceExists() || status == FileStatus::StatusMissing || status == FileStatus::StatusProxyOnly;
    if (!sourceMissing) {
        return err(QStringLiteral("Bin clip '%1' is not currently missing. Use bin_replace_source for intentional source replacement.").arg(binId));
    }

    QFileInfo replacement(requestedPath);
    if (replacement.isRelative()) replacement.setFile(QFileInfo(requestedPath).absoluteFilePath());
    if (!replacement.exists() || !replacement.isFile()) {
        return err(QStringLiteral("Relink target does not exist as a local file: %1").arg(replacement.absoluteFilePath()));
    }

    const QString oldPath = QFileInfo(clip->url()).absoluteFilePath();
    const int affectedInstances = clip->timelineInstances().size();
    pCore->bin()->replaceSingleClip(binId, replacement.absoluteFilePath());

    const std::shared_ptr<ProjectClip> live = model->getClipByBinID(binId);
    if (!live || QFileInfo(live->url()).absoluteFilePath() != replacement.absoluteFilePath()) {
        return err(QStringLiteral("Kdenlive relink command did not verify the replacement path on the live bin clip."));
    }
    if (!live->sourceExists() || live->clipStatus() == FileStatus::StatusMissing) {
        return err(QStringLiteral("The replacement path was applied, but Kdenlive still reports the source as unavailable."));
    }

    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("bin_id"), binId},
                       {QStringLiteral("old_path"), oldPath},
                       {QStringLiteral("path"), replacement.absoluteFilePath()},
                       {QStringLiteral("timeline_instances_affected"), affectedInstances},
                       {QStringLiteral("verified"), true}};
}
} // namespace

bool registerVibeCutRelinkTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject inputSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{
                                      {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                      {QStringLiteral("path"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                           {QStringLiteral("description"), QStringLiteral("Existing local file that should restore the missing bin asset.")}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id"), QStringLiteral("path")}},
                                  {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("bin_relink_missing")},
                             {QStringLiteral("description"), QStringLiteral("Relink a bin asset that Kdenlive currently reports missing/proxy-only to an existing local file. Refuses healthy assets, uses Kdenlive's native undoable replaceSingleClip/EditClipCommand path, and verifies the restored live source." )},
                             {QStringLiteral("input_schema"), inputSchema}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("bin_relink_missing");
    policy.risk = VibeCutToolRisk::MajorEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    return surface.registerTool(schema, policy, relinkMissing, error);
}
