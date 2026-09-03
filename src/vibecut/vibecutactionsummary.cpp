/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutactionsummary.h"

#include "vibecutmediaevidence.h"
#include "vibecuttoolsurface.h"

#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {
struct Prediction {
    QString sourceId;
    QString sourceFingerprint;
    QString extractorId;
    QString extractorVersion;
    QString model;
    QString modelRevision;
    QString taxonomy;
    QString actionSetHash;
    QString scoreSemantics;
    QString label;
    QString prompt;
    int labelId = -1;
    int start = -1;
    int end = -1;
    double score = -1.0;
    QJsonArray observedFrames;
};

struct Run { QList<Prediction> predictions; };

QString keyFor(const Prediction &p)
{
    return p.sourceId + QLatin1Char('\n') + p.sourceFingerprint + QLatin1Char('\n') + p.extractorId + QLatin1Char('\n') +
           p.extractorVersion + QLatin1Char('\n') + p.model + QLatin1Char('\n') + p.modelRevision + QLatin1Char('\n') +
           p.taxonomy + QLatin1Char('\n') + p.actionSetHash + QLatin1Char('\n') + p.scoreSemantics + QLatin1Char('\n') +
           p.label + QLatin1Char('\n') + QString::number(p.labelId);
}

bool parse(const QJsonObject &object, Prediction &p)
{
    if (object.value(QStringLiteral("kind")).toString() != QLatin1String("action_prediction")) return false;
    p.sourceId = object.value(QStringLiteral("source_id")).toString().trimmed();
    p.sourceFingerprint = object.value(QStringLiteral("source_fingerprint")).toString().trimmed();
    p.extractorId = object.value(QStringLiteral("extractor_id")).toString().trimmed();
    p.extractorVersion = object.value(QStringLiteral("extractor_version")).toString().trimmed();
    p.start = object.value(QStringLiteral("start_frame")).toInt(-1);
    p.end = object.value(QStringLiteral("end_frame")).toInt(-1);
    p.score = object.value(QStringLiteral("confidence")).toDouble(-1.0);
    const QJsonObject metadata = object.value(QStringLiteral("metadata")).toObject();
    p.model = metadata.value(QStringLiteral("model")).toString().trimmed();
    p.modelRevision = metadata.value(QStringLiteral("model_revision")).toString().trimmed();
    p.taxonomy = metadata.value(QStringLiteral("taxonomy")).toString().trimmed();
    p.actionSetHash = metadata.value(QStringLiteral("action_set_sha256")).toString().trimmed();
    p.scoreSemantics = metadata.value(QStringLiteral("score_semantics")).toString().trimmed();
    p.label = metadata.value(QStringLiteral("label")).toString().trimmed();
    p.prompt = metadata.value(QStringLiteral("prompt")).toString().trimmed();
    p.labelId = metadata.value(QStringLiteral("label_id")).toInt(-1);
    p.observedFrames = metadata.value(QStringLiteral("observed_frames")).toArray();
    return !p.sourceId.isEmpty() && !p.sourceFingerprint.isEmpty() && !p.extractorId.isEmpty() && !p.extractorVersion.isEmpty() &&
           !p.model.isEmpty() && !p.modelRevision.isEmpty() && !p.taxonomy.isEmpty() && !p.actionSetHash.isEmpty() &&
           !p.scoreSemantics.isEmpty() && !p.label.isEmpty() && !p.prompt.isEmpty() && p.labelId >= 0 &&
           p.start >= 0 && p.end > p.start && p.score >= 0.0 && p.score <= 1.0 && p.observedFrames.size() == 8;
}

QJsonObject runToJson(const Run &run)
{
    const Prediction &first = run.predictions.first();
    const Prediction &last = run.predictions.last();
    QJsonArray windows;
    QSet<int> frameSet;
    double sum = 0.0;
    double maximum = 0.0;
    for (const Prediction &p : run.predictions) {
        windows.append(QJsonObject{{QStringLiteral("start_frame"), p.start}, {QStringLiteral("end_frame"), p.end},
                                   {QStringLiteral("score"), p.score}, {QStringLiteral("observed_frames"), p.observedFrames}});
        for (const QJsonValue &value : p.observedFrames) frameSet.insert(value.toInt());
        sum += p.score;
        maximum = qMax(maximum, p.score);
    }
    QList<int> frames = frameSet.values();
    std::sort(frames.begin(), frames.end());
    QJsonArray observedFrames;
    for (int frame : frames) observedFrames.append(frame);
    const double count = static_cast<double>(run.predictions.size());
    return QJsonObject{
        {QStringLiteral("authority"), QStringLiteral("derived_prediction_summary")},
        {QStringLiteral("summary_kind"), QStringLiteral("action_prediction_track")},
        {QStringLiteral("source_id"), first.sourceId},
        {QStringLiteral("source_fingerprint"), first.sourceFingerprint},
        {QStringLiteral("extractor_id"), first.extractorId},
        {QStringLiteral("extractor_version"), first.extractorVersion},
        {QStringLiteral("model"), first.model},
        {QStringLiteral("model_revision"), first.modelRevision},
        {QStringLiteral("taxonomy"), first.taxonomy},
        {QStringLiteral("action_set_sha256"), first.actionSetHash},
        {QStringLiteral("score_semantics"), first.scoreSemantics},
        {QStringLiteral("label"), first.label},
        {QStringLiteral("label_id"), first.labelId},
        {QStringLiteral("prompt"), first.prompt},
        {QStringLiteral("inferred_start_frame"), first.start},
        {QStringLiteral("inferred_end_frame"), last.end},
        {QStringLiteral("window_count"), run.predictions.size()},
        {QStringLiteral("mean_relative_score"), sum / count},
        {QStringLiteral("maximum_relative_score"), maximum},
        {QStringLiteral("prediction_windows"), windows},
        {QStringLiteral("supporting_observed_frames"), observedFrames},
        {QStringLiteral("coverage_semantics"), QStringLiteral("prediction_windows_supported_by_eight_sampled_frames_each")},
        {QStringLiteral("note"), QStringLiteral("This summary groups repeated same-label X-CLIP predictions. Scores are relative softmax values over the fixed action set, not factual probabilities, and unsampled frames were not observed by the model.")},
    };
}

QJsonObject tool(const QJsonObject &input)
{
    QString error;
    const QJsonArray records = VibeCutMediaEvidence::loadCurrent(&error);
    if (!error.isEmpty()) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    const QJsonArray summaries = buildVibeCutActionSummaries(records,
                                                              input.value(QStringLiteral("source_id")).toString().trimmed(),
                                                              input.value(QStringLiteral("label")).toString().trimmed(),
                                                              input.value(QStringLiteral("min_score")).toDouble(0.20),
                                                              input.value(QStringLiteral("max_gap_frames")).toInt(0),
                                                              input.value(QStringLiteral("min_windows")).toInt(1));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("authority"), QStringLiteral("derived_prediction_summary")},
                       {QStringLiteral("summary_count"), summaries.size()},
                       {QStringLiteral("summaries"), summaries}};
}
}

QJsonArray buildVibeCutActionSummaries(const QJsonArray &records,
                                       const QString &sourceId,
                                       const QString &label,
                                       double minScore,
                                       int maxGapFrames,
                                       int minWindows)
{
    minScore = qBound(0.0, minScore, 1.0);
    maxGapFrames = qBound(0, maxGapFrames, 1000000);
    minWindows = qBound(1, minWindows, 1000);
    QList<Prediction> predictions;
    for (const QJsonValue &value : records) {
        if (!value.isObject()) continue;
        Prediction p;
        if (!parse(value.toObject(), p)) continue;
        if (!sourceId.isEmpty() && p.sourceId != sourceId) continue;
        if (!label.isEmpty() && p.label.compare(label, Qt::CaseInsensitive) != 0) continue;
        if (p.score < minScore) continue;
        predictions.append(p);
    }
    std::sort(predictions.begin(), predictions.end(), [](const Prediction &a, const Prediction &b) {
        const QString ak = keyFor(a), bk = keyFor(b);
        if (ak != bk) return ak < bk;
        if (a.start != b.start) return a.start < b.start;
        if (a.end != b.end) return a.end < b.end;
        return a.score > b.score;
    });

    QList<Run> runs;
    Run current;
    auto flush = [&]() {
        if (current.predictions.size() >= minWindows) runs.append(current);
        current = Run();
    };
    for (const Prediction &p : predictions) {
        if (!current.predictions.isEmpty()) {
            const Prediction &last = current.predictions.last();
            if (keyFor(last) != keyFor(p) || p.start > last.end + maxGapFrames) flush();
        }
        current.predictions.append(p);
    }
    flush();

    QJsonArray result;
    for (const Run &run : runs) result.append(runToJson(run));
    return result;
}

bool registerVibeCutActionSummaryTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("source_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("label"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 256}}},
                                {QStringLiteral("min_score"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                {QStringLiteral("max_gap_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 1000000}}},
                                {QStringLiteral("min_windows"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 1000}}}}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_action_tracks");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Group repeated same-label action_prediction windows using exact source/model/taxonomy/action-set provenance. Retains every prediction window and supporting eight-frame samples; relative softmax scores remain model-relative and are never promoted to factual probabilities or continuous observations.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, tool, error);
}
