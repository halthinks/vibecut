/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecudaudioeventsummary.h"

#include "vibecutmediaevidence.h"
#include "vibecuttoolsurface.h"

#include <QJsonObject>
#include <QVector>

#include <algorithm>

namespace {
struct Prediction
{
    QString recordId;
    QString sourceId;
    QString sourceFingerprint;
    QString extractorId;
    QString extractorVersion;
    QString model;
    QString taxonomy;
    QString label;
    int labelId = -1;
    int startFrame = -1;
    int endFrame = -1;
    int rank = -1;
    double score = -1.0;
};

struct Track
{
    Prediction first;
    int startFrame = -1;
    int endFrame = -1;
    double scoreSum = 0.0;
    double peakScore = 0.0;
    int count = 0;
    QJsonArray recordIds;
    QJsonArray windows;
};

bool parsePrediction(const QJsonObject &object, Prediction &prediction)
{
    if (object.value(QStringLiteral("kind")).toString() != QLatin1String("audio_event_prediction")) return false;
    const QJsonObject metadata = object.value(QStringLiteral("metadata")).toObject();
    if (metadata.value(QStringLiteral("authority")).toString() != QLatin1String("model_prediction")) return false;
    prediction.recordId = object.value(QStringLiteral("id")).toString();
    prediction.sourceId = object.value(QStringLiteral("source_id")).toString();
    prediction.sourceFingerprint = object.value(QStringLiteral("source_fingerprint")).toString();
    prediction.extractorId = object.value(QStringLiteral("extractor_id")).toString();
    prediction.extractorVersion = object.value(QStringLiteral("extractor_version")).toString();
    prediction.model = metadata.value(QStringLiteral("model")).toString();
    prediction.taxonomy = metadata.value(QStringLiteral("taxonomy")).toString();
    prediction.label = metadata.value(QStringLiteral("label")).toString();
    prediction.labelId = metadata.value(QStringLiteral("label_id")).toInt(-1);
    prediction.startFrame = object.value(QStringLiteral("start_frame")).toInt(-1);
    prediction.endFrame = object.value(QStringLiteral("end_frame")).toInt(-1);
    prediction.rank = metadata.value(QStringLiteral("rank")).toInt(-1);
    prediction.score = object.value(QStringLiteral("confidence")).toDouble(-1.0);
    return !prediction.sourceId.isEmpty() && !prediction.sourceFingerprint.isEmpty() && !prediction.extractorId.isEmpty() &&
           !prediction.extractorVersion.isEmpty() && !prediction.model.isEmpty() && !prediction.taxonomy.isEmpty() &&
           !prediction.label.isEmpty() && prediction.labelId >= 0 && prediction.startFrame >= 0 &&
           prediction.endFrame > prediction.startFrame && prediction.rank >= 1 && prediction.score >= 0.0 && prediction.score <= 1.0;
}

bool sameIdentity(const Prediction &a, const Prediction &b)
{
    return a.sourceId == b.sourceId && a.sourceFingerprint == b.sourceFingerprint &&
           a.extractorId == b.extractorId && a.extractorVersion == b.extractorVersion &&
           a.model == b.model && a.taxonomy == b.taxonomy && a.labelId == b.labelId &&
           a.label.compare(b.label, Qt::CaseSensitive) == 0;
}

QJsonObject trackToJson(const Track &track)
{
    const double mean = track.count > 0 ? track.scoreSum / track.count : 0.0;
    return QJsonObject{
        {QStringLiteral("authority"), QStringLiteral("derived_prediction_summary")},
        {QStringLiteral("kind"), QStringLiteral("audio_event_track")},
        {QStringLiteral("source_id"), track.first.sourceId},
        {QStringLiteral("source_fingerprint"), track.first.sourceFingerprint},
        {QStringLiteral("extractor_id"), track.first.extractorId},
        {QStringLiteral("extractor_version"), track.first.extractorVersion},
        {QStringLiteral("model"), track.first.model},
        {QStringLiteral("taxonomy"), track.first.taxonomy},
        {QStringLiteral("label"), track.first.label},
        {QStringLiteral("label_id"), track.first.labelId},
        {QStringLiteral("start_frame"), track.startFrame},
        {QStringLiteral("end_frame"), track.endFrame},
        {QStringLiteral("prediction_window_count"), track.count},
        {QStringLiteral("peak_score"), track.peakScore},
        {QStringLiteral("mean_score"), mean},
        {QStringLiteral("prediction_record_ids"), track.recordIds},
        {QStringLiteral("prediction_windows"), track.windows},
        {QStringLiteral("note"), QStringLiteral("This range summarizes recurring overlapping/adjacent model predictions. It does not convert the AudioSet label into an observed fact.")},
    };
}

QJsonObject tool(const QJsonObject &input)
{
    QString error;
    const QJsonArray records = VibeCutMediaEvidence::loadCurrent(&error);
    if (!error.isEmpty()) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    const QString sourceId = input.value(QStringLiteral("source_id")).toString().trimmed();
    const QString labelQuery = input.value(QStringLiteral("label_query")).toString().trimmed();
    const double minScore = qBound(0.0, input.value(QStringLiteral("min_score")).toDouble(0.10), 1.0);
    const int maxRank = qBound(1, input.value(QStringLiteral("max_rank")).toInt(8), 20);
    const int maxGapFrames = qBound(0, input.value(QStringLiteral("max_gap_frames")).toInt(0), 1000000);
    const QJsonArray tracks = buildVibeCutAudioEventTracks(records, sourceId, labelQuery, minScore, maxRank, maxGapFrames);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("authority"), QStringLiteral("derived_prediction_summary")},
                       {QStringLiteral("source_id"), sourceId},
                       {QStringLiteral("label_query"), labelQuery},
                       {QStringLiteral("min_score"), minScore},
                       {QStringLiteral("max_rank"), maxRank},
                       {QStringLiteral("max_gap_frames"), maxGapFrames},
                       {QStringLiteral("track_count"), tracks.size()},
                       {QStringLiteral("tracks"), tracks}};
}
}

QJsonArray buildVibeCutAudioEventTracks(const QJsonArray &records,
                                        const QString &sourceId,
                                        const QString &labelQuery,
                                        double minScore,
                                        int maxRank,
                                        int maxGapFrames)
{
    minScore = qBound(0.0, minScore, 1.0);
    maxRank = qBound(1, maxRank, 20);
    maxGapFrames = qBound(0, maxGapFrames, 1000000);
    const QString cleanSource = sourceId.trimmed();
    const QString cleanQuery = labelQuery.trimmed();

    QVector<Prediction> predictions;
    for (const QJsonValue &value : records) {
        if (!value.isObject()) continue;
        Prediction prediction;
        if (!parsePrediction(value.toObject(), prediction)) continue;
        if (!cleanSource.isEmpty() && prediction.sourceId != cleanSource) continue;
        if (!cleanQuery.isEmpty() && !prediction.label.contains(cleanQuery, Qt::CaseInsensitive)) continue;
        if (prediction.score < minScore || prediction.rank > maxRank) continue;
        predictions.append(prediction);
    }
    std::sort(predictions.begin(), predictions.end(), [](const Prediction &a, const Prediction &b) {
        if (a.sourceId != b.sourceId) return a.sourceId < b.sourceId;
        if (a.sourceFingerprint != b.sourceFingerprint) return a.sourceFingerprint < b.sourceFingerprint;
        if (a.extractorId != b.extractorId) return a.extractorId < b.extractorId;
        if (a.extractorVersion != b.extractorVersion) return a.extractorVersion < b.extractorVersion;
        if (a.model != b.model) return a.model < b.model;
        if (a.taxonomy != b.taxonomy) return a.taxonomy < b.taxonomy;
        if (a.labelId != b.labelId) return a.labelId < b.labelId;
        if (a.startFrame != b.startFrame) return a.startFrame < b.startFrame;
        return a.rank < b.rank;
    });

    QList<Track> completed;
    Track current;
    auto flush = [&]() {
        if (current.count > 0) completed.append(current);
        current = Track();
    };
    for (const Prediction &prediction : predictions) {
        const bool canJoin = current.count > 0 && sameIdentity(current.first, prediction) &&
                             static_cast<qint64>(prediction.startFrame) <= static_cast<qint64>(current.endFrame) + maxGapFrames;
        if (!canJoin) flush();
        if (current.count == 0) {
            current.first = prediction;
            current.startFrame = prediction.startFrame;
            current.endFrame = prediction.endFrame;
        } else {
            current.endFrame = qMax(current.endFrame, prediction.endFrame);
        }
        current.scoreSum += prediction.score;
        current.peakScore = qMax(current.peakScore, prediction.score);
        ++current.count;
        current.recordIds.append(prediction.recordId);
        current.windows.append(QJsonObject{{QStringLiteral("start_frame"), prediction.startFrame},
                                           {QStringLiteral("end_frame"), prediction.endFrame},
                                           {QStringLiteral("score"), prediction.score},
                                           {QStringLiteral("rank"), prediction.rank}});
    }
    flush();

    QJsonArray result;
    for (const Track &track : completed) result.append(trackToJson(track));
    return result;
}

bool registerVibeCutAudioEventSummaryTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("source_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("label_query"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 256}}},
                                {QStringLiteral("min_score"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                {QStringLiteral("max_rank"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 20}}},
                                {QStringLiteral("max_gap_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 1000000}}}}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_audio_event_tracks");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Summarize persisted ranked AudioSet model predictions into provenance-safe label tracks. Optional label_query supports questions such as speech, music, engine, alarm or wind. Tracks never cross source fingerprints/model versions and remain derived_prediction_summary rather than observed audio facts.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, tool, error);
}
