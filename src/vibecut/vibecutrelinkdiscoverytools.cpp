/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutrelinkdiscoverytools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "vibecuttoolsurface.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QJsonObject scanDirectory(const QJsonObject &input)
{
    if (!pCore) return err(QStringLiteral("Kdenlive core is unavailable."));
    const QString requested = input.value(QStringLiteral("directory")).toString().trimmed();
    if (requested.isEmpty()) return err(QStringLiteral("directory must not be empty"));

    QFileInfo rootInfo(requested);
    if (rootInfo.isRelative()) rootInfo.setFile(QFileInfo(requested).absoluteFilePath());
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        return err(QStringLiteral("Relink search directory does not exist: %1").arg(rootInfo.absoluteFilePath()));
    }

    const bool recursive = input.value(QStringLiteral("recursive")).toBool(true);
    QHash<QString, QStringList> byFileName;
    QDirIterator it(rootInfo.absoluteFilePath(), QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString fileName = QFileInfo(path).fileName();
        if (!fileName.isEmpty()) byFileName[fileName].append(QFileInfo(path).absoluteFilePath());
    }

    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) return err(QStringLiteral("Project bin model is unavailable."));

    QJsonArray exact;
    QJsonArray ambiguous;
    QJsonArray unmatched;
    for (const QString &binId : model->getAllClipIds()) {
        const std::shared_ptr<ProjectClip> clip = model->getClipByBinID(binId);
        if (!clip || !clip->hasUrl()) continue;
        const FileStatus::ClipStatus status = clip->clipStatus();
        const bool missing = !clip->sourceExists() || status == FileStatus::StatusMissing || status == FileStatus::StatusProxyOnly;
        if (!missing) continue;

        const QString expectedPath = QFileInfo(clip->url()).absoluteFilePath();
        const QString expectedName = QFileInfo(expectedPath).fileName();
        const QStringList matches = byFileName.value(expectedName);
        const QJsonObject base{{QStringLiteral("bin_id"), binId},
                               {QStringLiteral("expected_path"), expectedPath},
                               {QStringLiteral("expected_name"), expectedName},
                               {QStringLiteral("timeline_instances"), clip->timelineInstances().size()}};
        if (matches.size() == 1) {
            QJsonObject item = base;
            item.insert(QStringLiteral("path"), matches.first());
            item.insert(QStringLiteral("match_kind"), QStringLiteral("exact_filename"));
            exact.append(item);
        } else if (matches.size() > 1) {
            QJsonObject item = base;
            QJsonArray candidates;
            for (const QString &path : matches) candidates.append(path);
            item.insert(QStringLiteral("candidates"), candidates);
            item.insert(QStringLiteral("match_kind"), QStringLiteral("ambiguous_filename"));
            ambiguous.append(item);
        } else {
            unmatched.append(base);
        }
    }

    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("directory"), rootInfo.absoluteFilePath()},
                       {QStringLiteral("recursive"), recursive},
                       {QStringLiteral("exact_matches"), exact},
                       {QStringLiteral("ambiguous_matches"), ambiguous},
                       {QStringLiteral("unmatched"), unmatched},
                       {QStringLiteral("exact_count"), exact.size()},
                       {QStringLiteral("ambiguous_count"), ambiguous.size()},
                       {QStringLiteral("unmatched_count"), unmatched.size()}};
}
} // namespace

bool registerVibeCutRelinkDiscoveryTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("directory"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                          {QStringLiteral("description"), QStringLiteral("Existing local directory to search for missing-media filename matches.")}}},
                                {QStringLiteral("recursive"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                                                                          {QStringLiteral("description"), QStringLiteral("Search subdirectories. Defaults to true.")}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("directory")}},
                            {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("bin_relink_scan_directory")},
                             {QStringLiteral("description"), QStringLiteral("Read-only missing-media discovery. Scan a local directory and return exact filename matches, ambiguous candidates, and unmatched assets. Ambiguous matches are never auto-selected; review results before bin_relink_missing_batch.")},
                             {QStringLiteral("input_schema"), input}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("bin_relink_scan_directory");
    policy.risk = VibeCutToolRisk::ReadOnly;
    policy.reversible = false;
    policy.mutatesProject = false;
    return surface.registerTool(schema, policy, scanDirectory, error);
}
