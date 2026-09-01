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

struct RelinkTarget
{
    QString binId;
    QString oldPath;
    QString path;
    int affectedInstances{0};
};

bool validateRelinkTarget(const std::shared_ptr<ProjectItemModel> &model, const QString &binId, const QString &requestedPath,
                          RelinkTarget &target, QString &error)
{
    const std::shared_ptr<ProjectClip> clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip) {
        error = QStringLiteral("Bin clip '%1' does not exist.").arg(binId);
        return false;
    }
    if (!clip->hasUrl()) {
        error = QStringLiteral("Bin clip '%1' is generated/non-file-backed and cannot be relinked.").arg(binId);
        return false;
    }

    const FileStatus::ClipStatus status = clip->clipStatus();
    const bool sourceMissing = !clip->sourceExists() || status == FileStatus::StatusMissing || status == FileStatus::StatusProxyOnly;
    if (!sourceMissing) {
        error = QStringLiteral("Bin clip '%1' is not currently missing. Use bin_replace_source for intentional source replacement.").arg(binId);
        return false;
    }

    QFileInfo replacement(requestedPath.trimmed());
    if (replacement.isRelative()) replacement.setFile(QFileInfo(requestedPath).absoluteFilePath());
    if (!replacement.exists() || !replacement.isFile()) {
        error = QStringLiteral("Relink target does not exist as a local file: %1").arg(replacement.absoluteFilePath());
        return false;
    }

    target.binId = binId;
    target.oldPath = QFileInfo(clip->url()).absoluteFilePath();
    target.path = replacement.absoluteFilePath();
    target.affectedInstances = clip->timelineInstances().size();
    return true;
}

QJsonObject applyRelink(const std::shared_ptr<ProjectItemModel> &model, const RelinkTarget &target)
{
    pCore->bin()->replaceSingleClip(target.binId, target.path);
    const std::shared_ptr<ProjectClip> live = model->getClipByBinID(target.binId);
    if (!live || QFileInfo(live->url()).absoluteFilePath() != target.path) {
        return err(QStringLiteral("Kdenlive relink command did not verify replacement path for bin clip '%1'.").arg(target.binId));
    }
    if (!live->sourceExists() || live->clipStatus() == FileStatus::StatusMissing) {
        return err(QStringLiteral("Replacement was applied to bin clip '%1', but Kdenlive still reports the source unavailable.").arg(target.binId));
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("bin_id"), target.binId},
                       {QStringLiteral("old_path"), target.oldPath},
                       {QStringLiteral("path"), target.path},
                       {QStringLiteral("timeline_instances_affected"), target.affectedInstances},
                       {QStringLiteral("verified"), true}};
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
    RelinkTarget target;
    QString validationError;
    if (!validateRelinkTarget(model, binId, requestedPath, target, validationError)) return err(validationError);
    return applyRelink(model, target);
}

QJsonObject relinkMissingBatch(const QJsonObject &input)
{
    if (!pCore || !pCore->bin()) return err(QStringLiteral("Kdenlive project bin is unavailable."));
    const QJsonArray mappings = input.value(QStringLiteral("mappings")).toArray();
    if (mappings.isEmpty()) return err(QStringLiteral("mappings must contain at least one relink mapping"));
    if (mappings.size() > 200) return err(QStringLiteral("Batch relink is limited to 200 assets per operation."));

    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) return err(QStringLiteral("Project bin model is unavailable."));

    QVector<RelinkTarget> targets;
    QSet<QString> seen;
    QJsonArray preview;
    for (const QJsonValue &value : mappings) {
        if (!value.isObject()) return err(QStringLiteral("Every mappings entry must be an object."));
        const QJsonObject mapping = value.toObject();
        const QString binId = mapping.value(QStringLiteral("bin_id")).toString().trimmed();
        const QString path = mapping.value(QStringLiteral("path")).toString().trimmed();
        if (binId.isEmpty() || path.isEmpty()) return err(QStringLiteral("Every mapping requires non-empty bin_id and path."));
        if (seen.contains(binId)) return err(QStringLiteral("Duplicate bin_id in batch relink: %1").arg(binId));
        seen.insert(binId);

        RelinkTarget target;
        QString validationError;
        if (!validateRelinkTarget(model, binId, path, target, validationError)) {
            return err(QStringLiteral("Batch relink validation failed before any changes were made: %1").arg(validationError));
        }
        preview.append(QJsonObject{{QStringLiteral("bin_id"), target.binId},
                                   {QStringLiteral("old_path"), target.oldPath},
                                   {QStringLiteral("path"), target.path},
                                   {QStringLiteral("timeline_instances_affected"), target.affectedInstances}});
        targets.append(target);
    }

    if (input.value(QStringLiteral("dry_run")).toBool(false)) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("dry_run"), true},
                           {QStringLiteral("validated"), true}, {QStringLiteral("mappings"), preview},
                           {QStringLiteral("count"), targets.size()}};
    }

    QJsonArray results;
    for (const RelinkTarget &target : std::as_const(targets)) {
        const QJsonObject result = applyRelink(model, target);
        if (!result.value(QStringLiteral("ok")).toBool()) {
            return QJsonObject{{QStringLiteral("ok"), false},
                               {QStringLiteral("error"), QStringLiteral("Batch relink stopped because live verification failed. Earlier replacements remain individually undoable through Kdenlive's native command stack.")},
                               {QStringLiteral("failure"), result},
                               {QStringLiteral("completed"), results}};
        }
        results.append(result);
    }

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("dry_run"), false},
                       {QStringLiteral("count"), results.size()}, {QStringLiteral("results"), results},
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
                             {QStringLiteral("description"), QStringLiteral("Relink a bin asset that Kdenlive currently reports missing/proxy-only to an existing local file. Refuses healthy assets, uses Kdenlive's native undoable replaceSingleClip/EditClipCommand path, and verifies the restored live source.")},
                             {QStringLiteral("input_schema"), inputSchema}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("bin_relink_missing");
    policy.risk = VibeCutToolRisk::MajorEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    if (!surface.registerTool(schema, policy, relinkMissing, error)) return false;

    const QJsonObject mappingSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                    {QStringLiteral("properties"), QJsonObject{
                                        {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                        {QStringLiteral("path"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                    {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id"), QStringLiteral("path")}},
                                    {QStringLiteral("additionalProperties"), false}};
    const QJsonObject batchInput{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), QJsonObject{
                                     {QStringLiteral("mappings"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                              {QStringLiteral("minItems"), 1}, {QStringLiteral("maxItems"), 200},
                                                                              {QStringLiteral("items"), mappingSchema}}},
                                     {QStringLiteral("dry_run"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                                                                             {QStringLiteral("description"), QStringLiteral("Validate every mapping and return the proposed changes without modifying the project.")}}}}},
                                 {QStringLiteral("required"), QJsonArray{QStringLiteral("mappings")}},
                                 {QStringLiteral("additionalProperties"), false}};
    const QJsonObject batchSchema{{QStringLiteral("name"), QStringLiteral("bin_relink_missing_batch")},
                                  {QStringLiteral("description"), QStringLiteral("Validate a complete explicit missing-media relink mapping before any changes, optionally dry-run it, then apply every replacement through Kdenlive's native undoable source-replacement path with live verification.")},
                                  {QStringLiteral("input_schema"), batchInput}};
    VibeCutToolPolicy batchPolicy;
    batchPolicy.name = QStringLiteral("bin_relink_missing_batch");
    batchPolicy.risk = VibeCutToolRisk::MajorEdit;
    batchPolicy.reversible = true;
    batchPolicy.mutatesProject = true;
    return surface.registerTool(batchSchema, batchPolicy, relinkMissingBatch, error);
}
