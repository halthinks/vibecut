/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutmediaanalyzetools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "vibecutmediaevidence.h"
#include "vibecuttoolsurface.h"

#include <QHash>
#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QJsonObject analyze(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface || !pCore) return err(QStringLiteral("VibeCut/Kdenlive core is unavailable."));
    QString persistError;
    if (!VibeCutMediaEvidence::canPersistCurrent(&persistError)) return err(persistError);

    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty."));
    const bool onlyStale = input.value(QStringLiteral("only_stale")).toBool(true);
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    if (!clip->hasUrl()) return err(QStringLiteral("Basic media analysis currently requires a file-backed source."));

    QHash<QString, QString> statusByExtractor;
    QJsonObject freshnessResult;
    if (onlyStale) {
        freshnessResult = surface->invoke(QStringLiteral("media_evidence_freshness"), QJsonObject{{QStringLiteral("bin_id"), binId}});
        if (!freshnessResult.value(QStringLiteral("ok")).toBool(false)) return freshnessResult;
        for (const QJsonValue &value : freshnessResult.value(QStringLiteral("extractors")).toArray()) {
            const QJsonObject object = value.toObject();
            statusByExtractor.insert(object.value(QStringLiteral("extractor_id")).toString(), object.value(QStringLiteral("status")).toString());
        }
    }

    QJsonArray started;
    QJsonArray failed;
    QJsonArray skipped;
    auto invokeIfNeeded = [&](const QString &extractorId, const QString &tool, const QJsonObject &args) {
        if (onlyStale && statusByExtractor.value(extractorId) == QLatin1String("fresh")) {
            skipped.append(QJsonObject{{QStringLiteral("tool"), tool}, {QStringLiteral("extractor_id"), extractorId},
                                       {QStringLiteral("reason"), QStringLiteral("fresh")}});
            return;
        }
        const QJsonObject result = surface->invoke(tool, args);
        QJsonObject entry{{QStringLiteral("tool"), tool}, {QStringLiteral("extractor_id"), extractorId}, {QStringLiteral("result"), result}};
        if (result.value(QStringLiteral("ok")).toBool(false)) started.append(entry);
        else failed.append(entry);
    };

    invokeIfNeeded(QStringLiteral("source_metadata"), QStringLiteral("media_source_metadata_refresh"), QJsonObject{{QStringLiteral("bin_id"), binId}});
    if (clip->hasAudio()) {
        invokeIfNeeded(QStringLiteral("silence_detect"), QStringLiteral("media_silence_refresh"), QJsonObject{{QStringLiteral("bin_id"), binId}});
        invokeIfNeeded(QStringLiteral("loudness_detect"), QStringLiteral("media_loudness_refresh"), QJsonObject{{QStringLiteral("bin_id"), binId}});
        invokeIfNeeded(QStringLiteral("audio_r128"), QStringLiteral("media_audio_profile_refresh"),
                       QJsonObject{{QStringLiteral("bin_id"), binId},
                                   {QStringLiteral("sample_interval_ms"), 500},
                                   {QStringLiteral("max_samples"), 10000}});
    }
    if (clip->hasVideo()) {
        invokeIfNeeded(QStringLiteral("shot_boundary"), QStringLiteral("media_shots_refresh"), QJsonObject{{QStringLiteral("bin_id"), binId}});
        invokeIfNeeded(QStringLiteral("black_detect"), QStringLiteral("media_black_refresh"), QJsonObject{{QStringLiteral("bin_id"), binId}});
        invokeIfNeeded(QStringLiteral("freeze_detect"), QStringLiteral("media_freeze_refresh"), QJsonObject{{QStringLiteral("bin_id"), binId}});
        invokeIfNeeded(QStringLiteral("blur_detect"), QStringLiteral("media_blur_refresh"), QJsonObject{{QStringLiteral("bin_id"), binId}});
    }

    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("complete_start"), failed.isEmpty()},
                       {QStringLiteral("bin_id"), binId},
                       {QStringLiteral("only_stale"), onlyStale},
                       {QStringLiteral("has_audio"), clip->hasAudio()},
                       {QStringLiteral("has_video"), clip->hasVideo()},
                       {QStringLiteral("started_count"), started.size()},
                       {QStringLiteral("skipped_count"), skipped.size()},
                       {QStringLiteral("failed_count"), failed.size()},
                       {QStringLiteral("started"), started},
                       {QStringLiteral("skipped"), skipped},
                       {QStringLiteral("failed"), failed},
                       {QStringLiteral("freshness_before"), freshnessResult},
                       {QStringLiteral("asynchronous_children"), true}};
}
} // namespace

bool registerVibeCutMediaAnalyzeTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("only_stale"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                                                                           {QStringLiteral("description"), QStringLiteral("Default true. Skip deterministic extractor results whose source fingerprint and extractor version are already current.")}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                            {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("media_analyze_refresh")},
                             {QStringLiteral("description"), QStringLiteral("Run VibeCut's deterministic basic media-intelligence suite for one file-backed bin asset. By default launches only missing/stale extractors: source metadata always applicable; silence, source-wide loudness and bounded EBU R128 audio profile for audio; shot/black/freeze/blur for video. Child jobs use the shared JobManager and no project mutation occurs.")},
                             {QStringLiteral("input_schema"), input}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_analyze_refresh");
    policy.risk = VibeCutToolRisk::ExternalSideEffect;
    policy.asynchronous = true;
    policy.mutatesProject = false;
    return surface.registerTool(schema, policy, [&surface](const QJsonObject &request) { return analyze(&surface, request); }, error);
}
