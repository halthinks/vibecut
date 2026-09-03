/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutmediaevidencetools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "vibecutblackextractortools.h"
#include "vibecutblurextractortools.h"
#include "vibecutfreezeextractortools.h"
#include "vibecutloudnessextractortools.h"
#include "vibecutmediaanalyzetools.h"
#include "vibecutmediaevidence.h"
#include "vibecutr128extractortools.h"
#include "vibecutroomtone.h"
#include "vibecutshotextractortools.h"
#include "vibecutsilenceextractortools.h"
#include "vibecutsourceextractortools.h"
#include "vibecuttoolsurface.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString statFingerprint(const QFileInfo &info)
{
    const QByteArray payload = info.canonicalFilePath().toUtf8() + '\n' +
                               QByteArray::number(info.size()) + '\n' +
                               QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

QHash<QString, QString> expectedExtractorVersions()
{
    return QHash<QString, QString>{{QStringLiteral("source_metadata"), QStringLiteral("1.0.0")},
                                   {QStringLiteral("silence_detect"), QStringLiteral("1.0.0")},
                                   {QStringLiteral("loudness_detect"), QStringLiteral("1.0.0")},
                                   {QStringLiteral("audio_r128"), QStringLiteral("1.0.0")},
                                   {QStringLiteral("shot_boundary"), QStringLiteral("1.0.0")},
                                   {QStringLiteral("black_detect"), QStringLiteral("1.0.0")},
                                   {QStringLiteral("freeze_detect"), QStringLiteral("1.0.0")},
                                   {QStringLiteral("blur_detect"), QStringLiteral("1.0.0")}};
}

QJsonObject summary(const QJsonObject &)
{
    QString error;
    const QJsonArray records = VibeCutMediaEvidence::loadCurrent(&error);
    if (!error.isEmpty()) return err(error);

    QHash<QString, int> byKind;
    QHash<QString, int> byExtractor;
    QHash<QString, int> bySource;
    int withConfidence = 0;
    for (const QJsonValue &value : records) {
        const QJsonObject object = value.toObject();
        ++byKind[object.value(QStringLiteral("kind")).toString()];
        ++byExtractor[object.value(QStringLiteral("extractor_id")).toString()];
        ++bySource[object.value(QStringLiteral("source_id")).toString()];
        if (object.value(QStringLiteral("confidence")).toDouble(-1.0) >= 0.0) ++withConfidence;
    }

    auto counts = [](const QHash<QString, int> &source) {
        QJsonObject result;
        for (auto it = source.constBegin(); it != source.constEnd(); ++it) result.insert(it.key(), it.value());
        return result;
    };

    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("schema_version"), VibeCutMediaEvidence::SchemaVersion},
                       {QStringLiteral("record_count"), records.size()},
                       {QStringLiteral("source_count"), bySource.size()},
                       {QStringLiteral("extractor_count"), byExtractor.size()},
                       {QStringLiteral("records_with_confidence"), withConfidence},
                       {QStringLiteral("by_kind"), counts(byKind)},
                       {QStringLiteral("by_extractor"), counts(byExtractor)}};
}

QJsonObject listRecords(const QJsonObject &input)
{
    QString error;
    const QJsonArray records = VibeCutMediaEvidence::loadCurrent(&error);
    if (!error.isEmpty()) return err(error);

    const QString sourceId = input.value(QStringLiteral("source_id")).toString().trimmed();
    const QString extractorId = input.value(QStringLiteral("extractor_id")).toString().trimmed();
    const QString kind = input.value(QStringLiteral("kind")).toString().trimmed();
    const int limit = qBound(1, input.value(QStringLiteral("limit")).toInt(100), 1000);

    QJsonArray matches;
    for (const QJsonValue &value : records) {
        const QJsonObject object = value.toObject();
        if (!sourceId.isEmpty() && object.value(QStringLiteral("source_id")).toString() != sourceId) continue;
        if (!extractorId.isEmpty() && object.value(QStringLiteral("extractor_id")).toString() != extractorId) continue;
        if (!kind.isEmpty() && object.value(QStringLiteral("kind")).toString() != kind) continue;
        matches.append(object);
        if (matches.size() >= limit) break;
    }

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("record_count"), matches.size()},
                       {QStringLiteral("records"), matches},
                       {QStringLiteral("filters"), QJsonObject{{QStringLiteral("source_id"), sourceId},
                                                                {QStringLiteral("extractor_id"), extractorId},
                                                                {QStringLiteral("kind"), kind}}}};
}

QJsonObject freshness(const QJsonObject &input)
{
    if (!pCore) return err(QStringLiteral("Kdenlive core is unavailable."));
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty."));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    if (!clip->hasUrl()) return err(QStringLiteral("Evidence freshness currently requires a file-backed source."));
    const QFileInfo info(clip->url());
    if (!info.exists() || !info.isFile()) return err(QStringLiteral("Source file is missing or invalid."));
    const QString currentFingerprint = statFingerprint(info);
    const QString sourceId = QStringLiteral("bin:%1").arg(binId);

    QString error;
    const QJsonArray records = VibeCutMediaEvidence::loadCurrent(&error);
    if (!error.isEmpty()) return err(error);

    struct State { QString version; QString fingerprint; int count = 0; };
    QHash<QString, State> states;
    for (const QJsonValue &value : records) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("source_id")).toString() != sourceId) continue;
        const QString extractor = object.value(QStringLiteral("extractor_id")).toString();
        State state = states.value(extractor);
        state.version = object.value(QStringLiteral("extractor_version")).toString();
        state.fingerprint = object.value(QStringLiteral("source_fingerprint")).toString();
        ++state.count;
        states.insert(extractor, state);
    }

    const QHash<QString, QString> expectedVersions = expectedExtractorVersions();
    const QStringList expected = expectedVersions.keys();
    QJsonArray extractorStates;
    int freshCount = 0;
    int staleCount = 0;
    int missingCount = 0;
    for (const QString &extractor : expected) {
        const bool applicable = extractor == QLatin1String("source_metadata") ||
                                ((extractor == QLatin1String("silence_detect") || extractor == QLatin1String("loudness_detect") ||
                                  extractor == QLatin1String("audio_r128")) && clip->hasAudio()) ||
                                ((extractor == QLatin1String("shot_boundary") || extractor == QLatin1String("black_detect") ||
                                  extractor == QLatin1String("freeze_detect") || extractor == QLatin1String("blur_detect")) && clip->hasVideo());
        if (!applicable) continue;
        const bool present = states.contains(extractor);
        const State state = states.value(extractor);
        const QString expectedVersion = expectedVersions.value(extractor);
        const bool fingerprintFresh = present && state.fingerprint == currentFingerprint;
        const bool versionFresh = present && state.version == expectedVersion;
        const bool fresh = fingerprintFresh && versionFresh;
        QString status;
        QString staleReason;
        if (!present) {
            status = QStringLiteral("missing");
            ++missingCount;
        } else if (!fresh) {
            status = QStringLiteral("stale");
            if (!fingerprintFresh && !versionFresh) staleReason = QStringLiteral("source_and_extractor_version_changed");
            else if (!fingerprintFresh) staleReason = QStringLiteral("source_changed");
            else staleReason = QStringLiteral("extractor_version_changed");
            ++staleCount;
        } else {
            status = QStringLiteral("fresh");
            ++freshCount;
        }
        extractorStates.append(QJsonObject{{QStringLiteral("extractor_id"), extractor},
                                            {QStringLiteral("status"), status},
                                            {QStringLiteral("stale_reason"), staleReason},
                                            {QStringLiteral("record_count"), state.count},
                                            {QStringLiteral("extractor_version"), state.version},
                                            {QStringLiteral("expected_extractor_version"), expectedVersion},
                                            {QStringLiteral("stored_fingerprint"), state.fingerprint},
                                            {QStringLiteral("current_fingerprint"), currentFingerprint}});
    }

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("bin_id"), binId},
                       {QStringLiteral("source_id"), sourceId}, {QStringLiteral("current_fingerprint"), currentFingerprint},
                       {QStringLiteral("fresh_count"), freshCount}, {QStringLiteral("stale_count"), staleCount},
                       {QStringLiteral("missing_count"), missingCount},
                       {QStringLiteral("analysis_current"), staleCount == 0 && missingCount == 0},
                       {QStringLiteral("extractors"), extractorStates}};
}
} // namespace

bool registerVibeCutMediaEvidenceTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy summaryPolicy;
    summaryPolicy.name = QStringLiteral("media_evidence_summary");
    summaryPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), summaryPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Summarize the persistent extractor-produced media evidence ledger by source, extractor and evidence kind. Read-only; the model cannot write evidence through this surface.")},
                                          {QStringLiteral("input_schema"), noArgs}},
                              summaryPolicy, summary, error)) return false;

    const QJsonObject listInput{{QStringLiteral("type"), QStringLiteral("object")},
                                {QStringLiteral("properties"), QJsonObject{
                                    {QStringLiteral("source_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                    {QStringLiteral("extractor_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                    {QStringLiteral("kind"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                    {QStringLiteral("limit"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 1000}}}}},
                                {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy listPolicy;
    listPolicy.name = QStringLiteral("media_evidence_list");
    listPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), listPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("List persistent extractor-produced evidence records with optional source/extractor/kind filters, including provenance, confidence and source fingerprint. Read-only.")},
                                          {QStringLiteral("input_schema"), listInput}},
                              listPolicy, listRecords, error)) return false;

    const QJsonObject freshnessInput{{QStringLiteral("type"), QStringLiteral("object")},
                                     {QStringLiteral("properties"), QJsonObject{{QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                     {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                                     {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy freshnessPolicy;
    freshnessPolicy.name = QStringLiteral("media_evidence_freshness");
    freshnessPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), freshnessPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Compare persistent extractor source fingerprints and extractor versions for one file-backed bin asset against the current source and built-in extractor versions; report each applicable extractor as fresh, stale, or missing. Read-only.")},
                                          {QStringLiteral("input_schema"), freshnessInput}},
                              freshnessPolicy, freshness, error)) return false;

    if (!registerVibeCutSourceExtractorTools(surface, error)) return false;
    if (!registerVibeCutSilenceExtractorTools(surface, error)) return false;
    if (!registerVibeCutLoudnessExtractorTools(surface, error)) return false;
    if (!registerVibeCutR128ExtractorTools(surface, error)) return false;
    if (!registerVibeCutRoomToneTools(surface, error)) return false;
    if (!registerVibeCutShotExtractorTools(surface, error)) return false;
    if (!registerVibeCutBlackExtractorTools(surface, error)) return false;
    if (!registerVibeCutFreezeExtractorTools(surface, error)) return false;
    if (!registerVibeCutBlurExtractorTools(surface, error)) return false;
    return registerVibeCutMediaAnalyzeTools(surface, error);
}
