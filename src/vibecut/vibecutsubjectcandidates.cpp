/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutsubjectcandidates.h"

#include "vibecutmediaevidence.h"
#include "vibecutobjecttracks.h"
#include "vibecuttoolsurface.h"

#include <QJsonObject>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {
QJsonObject tool(const QJsonObject &input)
{
    QString error;
    const QJsonArray records = VibeCutMediaEvidence::loadCurrent(&error);
    if (!error.isEmpty()) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    const QJsonArray candidates = buildVibeCutSubjectCandidates(
        records,
        input.value(QStringLiteral("source_id")).toString().trimmed(),
        input.value(QStringLiteral("label")).toString().trimmed(),
        input.value(QStringLiteral("min_score")).toDouble(0.50),
        input.value(QStringLiteral("min_iou")).toDouble(0.25),
        input.value(QStringLiteral("max_gap_steps")).toInt(2),
        input.value(QStringLiteral("min_observations")).toInt(2),
        input.value(QStringLiteral("limit")).toInt(20));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("authority"), QStringLiteral("derived_candidate")},
                       {QStringLiteral("candidate_count"), candidates.size()},
                       {QStringLiteral("candidates"), candidates}};
}
}

QJsonArray buildVibeCutSubjectCandidates(const QJsonArray &records,
                                         const QString &sourceId,
                                         const QString &label,
                                         double minScore,
                                         double minIou,
                                         int maxGapSteps,
                                         int minObservations,
                                         int limit)
{
    limit = qBound(1, limit, 100);
    const QJsonArray tracks = buildVibeCutObjectTracks(records, sourceId, label, minScore, minIou, maxGapSteps, minObservations);
    QList<QJsonObject> ranked;
    for (const QJsonValue &value : tracks) {
        if (!value.isObject()) continue;
        QJsonObject candidate = value.toObject();
        const int observationCount = candidate.value(QStringLiteral("observation_count")).toInt(0);
        const int sampleInterval = candidate.value(QStringLiteral("sample_interval_frames")).toInt(1);
        const int start = candidate.value(QStringLiteral("inferred_start_frame")).toInt(0);
        const int end = candidate.value(QStringLiteral("inferred_end_frame")).toInt(start + 1);
        const double confidence = qBound(0.0, candidate.value(QStringLiteral("mean_prediction_score")).toDouble(0.0), 1.0);
        const double area = qBound(0.0, candidate.value(QStringLiteral("mean_box_area_fraction")).toDouble(0.0), 1.0);
        const double center = qBound(0.0, candidate.value(QStringLiteral("mean_center_proximity")).toDouble(0.0), 1.0);
        const int theoreticalSamples = qMax(1, 1 + qMax(0, end - 1 - start) / qMax(1, sampleInterval));
        const double density = qBound(0.0, static_cast<double>(observationCount) / theoreticalSamples, 1.0);
        // Transparent editorial-prominence heuristic. sqrt(area) makes a
        // moderately sized centered subject competitive without requiring it
        // to occupy most of the frame.
        const double areaComponent = std::sqrt(area);
        const double prominence = qBound(0.0,
                                         0.35 * confidence + 0.30 * areaComponent + 0.20 * center + 0.15 * density,
                                         1.0);
        candidate.insert(QStringLiteral("authority"), QStringLiteral("derived_candidate"));
        candidate.insert(QStringLiteral("candidate_kind"), QStringLiteral("editorial_visual_subject"));
        candidate.insert(QStringLiteral("prominence_score"), prominence);
        candidate.insert(QStringLiteral("prominence_components"),
                         QJsonObject{{QStringLiteral("mean_prediction_score"), confidence},
                                     {QStringLiteral("sqrt_mean_box_area_fraction"), areaComponent},
                                     {QStringLiteral("mean_center_proximity"), center},
                                     {QStringLiteral("observed_sample_density"), density},
                                     {QStringLiteral("weights"), QJsonObject{{QStringLiteral("confidence"), 0.35},
                                                                             {QStringLiteral("area"), 0.30},
                                                                             {QStringLiteral("center"), 0.20},
                                                                             {QStringLiteral("density"), 0.15}}}});
        candidate.insert(QStringLiteral("note"), QStringLiteral("This is a transparent editorial prominence candidate derived from sampled object-model predictions. It is not object/person identity, importance fact, or evidence for unsampled frames."));
        ranked.append(candidate);
    }
    std::sort(ranked.begin(), ranked.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const double as = a.value(QStringLiteral("prominence_score")).toDouble();
        const double bs = b.value(QStringLiteral("prominence_score")).toDouble();
        if (qAbs(as - bs) > 1e-12) return as > bs;
        const int ac = a.value(QStringLiteral("observation_count")).toInt();
        const int bc = b.value(QStringLiteral("observation_count")).toInt();
        if (ac != bc) return ac > bc;
        if (a.value(QStringLiteral("label")).toString() != b.value(QStringLiteral("label")).toString())
            return a.value(QStringLiteral("label")).toString() < b.value(QStringLiteral("label")).toString();
        return a.value(QStringLiteral("inferred_start_frame")).toInt() < b.value(QStringLiteral("inferred_start_frame")).toInt();
    });
    QJsonArray result;
    for (int i = 0; i < ranked.size() && i < limit; ++i) {
        QJsonObject candidate = ranked.at(i);
        candidate.insert(QStringLiteral("rank"), i + 1);
        result.append(candidate);
    }
    return result;
}

bool registerVibeCutSubjectCandidateTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("source_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("label"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 256}}},
                                {QStringLiteral("min_score"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                {QStringLiteral("min_iou"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                {QStringLiteral("max_gap_steps"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 20}}},
                                {QStringLiteral("min_observations"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 2}, {QStringLiteral("maximum"), 1000}}},
                                {QStringLiteral("limit"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 100}}}}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_subject_candidates");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Rank provenance-safe object continuity tracks as reviewable editorial visual-subject candidates using a transparent weighted score over model confidence, normalized screen area, center proximity and observed-sample density. Candidates are not identity or importance facts.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, tool, error);
}
