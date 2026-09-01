/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutsourceextractortools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "vibecutmediaevidence.h"
#include "vibecuttoolsurface.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>

namespace {
constexpr auto ExtractorId = "source_metadata";
constexpr auto ExtractorVersion = "1.0.0";

QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString clipName(const std::shared_ptr<ProjectClip> &clip)
{
    QString name = clip ? clip->getProducerProperty(QStringLiteral("kdenlive:clipname")) : QString();
    if (name.isEmpty() && clip && !clip->url().isEmpty()) name = QFileInfo(clip->url()).fileName();
    return name;
}

QString statFingerprint(const QFileInfo &info)
{
    const QByteArray payload = info.canonicalFilePath().toUtf8() + '\n' +
                               QByteArray::number(info.size()) + '\n' +
                               QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

bool refreshOne(const QString &binId, QJsonObject &result, QString &error)
{
    if (!pCore) {
        error = QStringLiteral("Kdenlive core is unavailable.");
        return false;
    }
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip) {
        error = QStringLiteral("Bin clip '%1' does not exist.").arg(binId);
        return false;
    }
    if (!clip->hasUrl()) {
        error = QStringLiteral("Bin clip '%1' is not file-backed; source_metadata only extracts local file-backed media.").arg(binId);
        return false;
    }

    const QFileInfo info(clip->url());
    if (!info.exists() || !info.isFile()) {
        error = QStringLiteral("Source file for bin clip '%1' is missing or is not a regular file.").arg(binId);
        return false;
    }

    const QString fingerprint = statFingerprint(info);
    VibeCutMediaEvidenceRecord record;
    record.id = QStringLiteral("source_metadata:%1:%2").arg(binId, fingerprint.left(16));
    record.sourceId = QStringLiteral("bin:%1").arg(binId);
    record.sourceFingerprint = fingerprint;
    record.extractorId = QString::fromLatin1(ExtractorId);
    record.extractorVersion = QString::fromLatin1(ExtractorVersion);
    record.kind = QStringLiteral("source_metadata");
    record.startFrame = 0;
    record.endFrame = qMax(0, clip->getFramePlaytime());
    record.text = QStringLiteral("%1 %2").arg(clipName(clip), info.fileName()).trimmed();
    record.confidence = 1.0;
    record.producedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    record.metadata = QJsonObject{{QStringLiteral("bin_id"), binId},
                                  {QStringLiteral("path"), info.absoluteFilePath()},
                                  {QStringLiteral("canonical_path"), info.canonicalFilePath()},
                                  {QStringLiteral("file_name"), info.fileName()},
                                  {QStringLiteral("suffix"), info.suffix().toLower()},
                                  {QStringLiteral("size_bytes"), static_cast<double>(info.size())},
                                  {QStringLiteral("modified_utc"), info.lastModified().toUTC().toString(Qt::ISODateWithMs)},
                                  {QStringLiteral("fingerprint_kind"), QStringLiteral("sha256(path,size,mtime)")},
                                  {QStringLiteral("clip_type"), static_cast<int>(clip->clipType())},
                                  {QStringLiteral("has_audio"), clip->hasAudio()},
                                  {QStringLiteral("has_video"), clip->hasVideo()},
                                  {QStringLiteral("duration_frames"), clip->getFramePlaytime()},
                                  {QStringLiteral("timeline_instances"), static_cast<int>(clip->timelineInstances().size())}};

    if (!VibeCutMediaEvidence::replaceSourceExtractorCurrent(record.sourceId, fingerprint,
                                                              record.extractorId, record.extractorVersion,
                                                              QList<VibeCutMediaEvidenceRecord>{record}, &error)) {
        return false;
    }

    result = QJsonObject{{QStringLiteral("bin_id"), binId},
                         {QStringLiteral("source_id"), record.sourceId},
                         {QStringLiteral("source_fingerprint"), fingerprint},
                         {QStringLiteral("extractor_id"), record.extractorId},
                         {QStringLiteral("extractor_version"), record.extractorVersion},
                         {QStringLiteral("record_id"), record.id},
                         {QStringLiteral("path"), info.absoluteFilePath()},
                         {QStringLiteral("size_bytes"), static_cast<double>(info.size())},
                         {QStringLiteral("verified"), true}};
    return true;
}

QJsonObject refresh(const QJsonObject &input)
{
    if (!pCore) return err(QStringLiteral("Kdenlive core is unavailable."));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) return err(QStringLiteral("Project bin model is unavailable."));

    QStringList ids;
    const QString requested = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (!requested.isEmpty()) {
        ids << requested;
    } else {
        for (const QString &binId : model->getAllClipIds()) {
            const std::shared_ptr<ProjectClip> clip = model->getClipByBinID(binId);
            if (clip && clip->hasUrl()) ids << binId;
        }
    }
    if (ids.isEmpty()) return err(QStringLiteral("No file-backed bin assets are available for source metadata extraction."));
    if (ids.size() > 5000) return err(QStringLiteral("Source metadata refresh is limited to 5000 assets per operation."));

    QJsonArray refreshed;
    QJsonArray skipped;
    for (const QString &binId : ids) {
        QJsonObject item;
        QString error;
        if (refreshOne(binId, item, error)) {
            refreshed.append(item);
        } else {
            if (!requested.isEmpty()) return err(error);
            skipped.append(QJsonObject{{QStringLiteral("bin_id"), binId}, {QStringLiteral("reason"), error}});
        }
    }

    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("extractor_id"), QString::fromLatin1(ExtractorId)},
                       {QStringLiteral("extractor_version"), QString::fromLatin1(ExtractorVersion)},
                       {QStringLiteral("refreshed_count"), refreshed.size()},
                       {QStringLiteral("skipped_count"), skipped.size()},
                       {QStringLiteral("refreshed"), refreshed},
                       {QStringLiteral("skipped"), skipped}};
}
} // namespace

bool registerVibeCutSourceExtractorTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                       {QStringLiteral("description"), QStringLiteral("Optional single file-backed bin asset. Omit to refresh all file-backed bin assets.")}}}}},
                            {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("media_source_metadata_refresh")},
                             {QStringLiteral("description"), QStringLiteral("Run the deterministic VibeCut source-metadata extractor for one or all file-backed bin assets. Writes versioned extractor evidence containing source identity, stat fingerprint, file metadata, A/V capability and duration. This is extractor-owned evidence generation, not arbitrary model-authored evidence.")},
                             {QStringLiteral("input_schema"), input}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_source_metadata_refresh");
    policy.risk = VibeCutToolRisk::ExternalSideEffect;
    policy.mutatesProject = false;
    policy.reversible = false;
    return surface.registerTool(schema, policy, refresh, error);
}
