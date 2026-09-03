/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutobjecttracks.h"

#include "vibecutmediaevidence.h"
#include "vibecuttoolsurface.h"

#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace {
struct Detection {
    QString sourceId;
    QString sourceFingerprint;
    QString extractorId;
    QString extractorVersion;
    QString model;
    QString modelRevision;
    QString taxonomy;
    QString label;
    int labelId = -1;
    int frame = -1;
    int sampleIntervalFrames = -1;
    double score = -1.0;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct Track {
    QList<Detection> observations;
};

QString keyFor(const Detection &d)
{
    return d.sourceId + QLatin1Char('\n') + d.sourceFingerprint + QLatin1Char('\n') + d.extractorId + QLatin1Char('\n') +
           d.extractorVersion + QLatin1Char('\n') + d.model + QLatin1Char('\n') + d.modelRevision + QLatin1Char('\n') +
           d.taxonomy + QLatin1Char('\n') + d.label + QLatin1Char('\n') + QString::number(d.labelId) + QLatin1Char('\n') +
           QString::number(d.sampleIntervalFrames);
}

bool parseDetection(const QJsonObject &object, Detection &d)
{
    if (object.value(QStringLiteral("kind")).toString() != QLatin1String("object_detection_prediction")) return false;
    d.sourceId = object.value(QStringLiteral("source_id")).toString().trimmed();
    d.sourceFingerprint = object.value(QStringLiteral("source_fingerprint")).toString().trimmed();
    d.extractorId = object.value(QStringLiteral("extractor_id")).toString().trimmed();
    d.extractorVersion = object.value(QStringLiteral("extractor_version")).toString().trimmed();
    d.frame = object.value(QStringLiteral("start_frame")).toInt(-1);
    d.score = object.value(QStringLiteral("confidence")).toDouble(-1.0);
    const QJsonObject metadata = object.value(QStringLiteral("metadata")).toObject();
    d.model = metadata.value(QStringLiteral("model")).toString().trimmed();
    d.modelRevision = metadata.value(QStringLiteral("model_revision")).toString().trimmed();
    d.taxonomy = metadata.value(QStringLiteral("taxonomy")).toString().trimmed();
    d.label = metadata.value(QStringLiteral("label")).toString().trimmed();
    d.labelId = metadata.value(QStringLiteral("label_id")).toInt(-1);
    d.sampleIntervalFrames = metadata.value(QStringLiteral("sample_interval_frames")).toInt(-1);
    const int imageWidth = metadata.value(QStringLiteral("image_width")).toInt(-1);
    const int imageHeight = metadata.value(QStringLiteral("image_height")).toInt(-1);
    const QJsonObject box = metadata.value(QStringLiteral("bbox_pixels")).toObject();
    const int x = box.value(QStringLiteral("x")).toInt(-1);
    const int y = box.value(QStringLiteral("y")).toInt(-1);
    const int width = box.value(QStringLiteral("width")).toInt(-1);
    const int height = box.value(QStringLiteral("height")).toInt(-1);
    if (d.sourceId.isEmpty() || d.sourceFingerprint.isEmpty() || d.extractorId.isEmpty() || d.extractorVersion.isEmpty() ||
        d.model.isEmpty() || d.modelRevision.isEmpty() || d.taxonomy.isEmpty() || d.label.isEmpty() || d.labelId < 0 ||
        d.frame < 0 || d.sampleIntervalFrames <= 0 || d.score < 0.0 || d.score > 1.0 || imageWidth <= 0 || imageHeight <= 0 ||
        x < 0 || y < 0 || width <= 0 || height <= 0 ||
        static_cast<qint64>(x) + width > imageWidth || static_cast<qint64>(y) + height > imageHeight) return false;
    d.x = static_cast<double>(x) / imageWidth;
    d.y = static_cast<double>(y) / imageHeight;
    d.width = static_cast<double>(width) / imageWidth;
    d.height = static_cast<double>(height) / imageHeight;
    return true;
}

double iou(const Detection &a, const Detection &b)
{
    const double left = qMax(a.x, b.x);
    const double top = qMax(a.y, b.y);
    const double right = qMin(a.x + a.width, b.x + b.width);
    const double bottom = qMin(a.y + a.height, b.y + b.height);
    const double intersection = qMax(0.0, right - left) * qMax(0.0, bottom - top);
    const double unionArea = a.width * a.height + b.width * b.height - intersection;
    return unionArea > 0.0 ? intersection / unionArea : 0.0;
}

QJsonObject trackToJson(const Track &track)
{
    const Detection &first = track.observations.first();
    const Detection &last = track.observations.last();
    QJsonArray frames;
    QJsonArray boxes;
    QJsonArray unobserved;
    double sumScore = 0.0;
    double minScore = 1.0;
    double sumArea = 0.0;
    double sumCenterProximity = 0.0;
    for (int i = 0; i < track.observations.size(); ++i) {
        const Detection &d = track.observations.at(i);
        frames.append(d.frame);
        boxes.append(QJsonObject{{QStringLiteral("frame"), d.frame},
                                 {QStringLiteral("x"), d.x}, {QStringLiteral("y"), d.y},
                                 {QStringLiteral("width"), d.width}, {QStringLiteral("height"), d.height},
                                 {QStringLiteral("score"), d.score}});
        sumScore += d.score;
        minScore = qMin(minScore, d.score);
        sumArea += d.width * d.height;
        const double cx = d.x + d.width * 0.5;
        const double cy = d.y + d.height * 0.5;
        const double distance = std::sqrt((cx - 0.5) * (cx - 0.5) + (cy - 0.5) * (cy - 0.5));
        const double maxDistance = std::sqrt(0.5);
        sumCenterProximity += qBound(0.0, 1.0 - distance / maxDistance, 1.0);
        if (i > 0) {
            const int previous = track.observations.at(i - 1).frame;
            if (d.frame > previous + 1) {
                unobserved.append(QJsonObject{{QStringLiteral("start_frame"), previous + 1},
                                              {QStringLiteral("end_frame"), d.frame}});
            }
        }
    }
    const double count = static_cast<double>(track.observations.size());
    return QJsonObject{
        {QStringLiteral("authority"), QStringLiteral("derived_prediction_track")},
        {QStringLiteral("track_kind"), QStringLiteral("object_continuity")},
        {QStringLiteral("source_id"), first.sourceId},
        {QStringLiteral("source_fingerprint"), first.sourceFingerprint},
        {QStringLiteral("extractor_id"), first.extractorId},
        {QStringLiteral("extractor_version"), first.extractorVersion},
        {QStringLiteral("model"), first.model},
        {QStringLiteral("model_revision"), first.modelRevision},
        {QStringLiteral("taxonomy"), first.taxonomy},
        {QStringLiteral("label"), first.label},
        {QStringLiteral("label_id"), first.labelId},
        {QStringLiteral("sample_interval_frames"), first.sampleIntervalFrames},
        {QStringLiteral("inferred_start_frame"), first.frame},
        {QStringLiteral("inferred_end_frame"), last.frame + 1},
        {QStringLiteral("observation_count"), track.observations.size()},
        {QStringLiteral("observed_frames"), frames},
        {QStringLiteral("observed_boxes_normalized"), boxes},
        {QStringLiteral("unobserved_intervals"), unobserved},
        {QStringLiteral("mean_prediction_score"), sumScore / count},
        {QStringLiteral("minimum_prediction_score"), minScore},
        {QStringLiteral("mean_box_area_fraction"), sumArea / count},
        {QStringLiteral("mean_center_proximity"), sumCenterProximity / count},
        {QStringLiteral("coverage_semantics"), QStringLiteral("sampled_predictions_only")},
        {QStringLiteral("note"), QStringLiteral("Continuity is inferred from repeated same-label model predictions with geometry overlap. This track is not object/person identity and does not claim the object was observed on unsampled frames.")},
    };
}

QJsonObject tool(const QJsonObject &input)
{
    QString error;
    const QJsonArray records = VibeCutMediaEvidence::loadCurrent(&error);
    if (!error.isEmpty()) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    const QString sourceId = input.value(QStringLiteral("source_id")).toString().trimmed();
    const QString label = input.value(QStringLiteral("label")).toString().trimmed();
    const double minScore = input.value(QStringLiteral("min_score")).toDouble(0.50);
    const double minIou = input.value(QStringLiteral("min_iou")).toDouble(0.25);
    const int maxGapSteps = input.value(QStringLiteral("max_gap_steps")).toInt(2);
    const int minObservations = input.value(QStringLiteral("min_observations")).toInt(2);
    const QJsonArray tracks = buildVibeCutObjectTracks(records, sourceId, label, minScore, minIou, maxGapSteps, minObservations);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("authority"), QStringLiteral("derived_prediction_summary")},
                       {QStringLiteral("track_count"), tracks.size()},
                       {QStringLiteral("tracks"), tracks}};
}
}

QJsonArray buildVibeCutObjectTracks(const QJsonArray &records,
                                    const QString &sourceId,
                                    const QString &label,
                                    double minScore,
                                    double minIou,
                                    int maxGapSteps,
                                    int minObservations)
{
    minScore = qBound(0.0, minScore, 1.0);
    minIou = qBound(0.0, minIou, 1.0);
    maxGapSteps = qBound(1, maxGapSteps, 20);
    minObservations = qBound(2, minObservations, 1000);

    QList<Detection> detections;
    for (const QJsonValue &value : records) {
        if (!value.isObject()) continue;
        Detection d;
        if (!parseDetection(value.toObject(), d)) continue;
        if (!sourceId.isEmpty() && d.sourceId != sourceId) continue;
        if (!label.isEmpty() && d.label.compare(label, Qt::CaseInsensitive) != 0) continue;
        if (d.score < minScore) continue;
        detections.append(d);
    }
    std::sort(detections.begin(), detections.end(), [](const Detection &a, const Detection &b) {
        const QString ak = keyFor(a);
        const QString bk = keyFor(b);
        if (ak != bk) return ak < bk;
        if (a.frame != b.frame) return a.frame < b.frame;
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.score > b.score;
    });

    QList<Track> tracks;
    for (const Detection &d : detections) {
        int bestTrack = -1;
        double bestIou = -1.0;
        for (int i = 0; i < tracks.size(); ++i) {
            if (tracks.at(i).observations.isEmpty()) continue;
            const Detection &last = tracks.at(i).observations.last();
            if (keyFor(last) != keyFor(d) || d.frame <= last.frame) continue;
            const qint64 gap = static_cast<qint64>(d.frame) - last.frame;
            const qint64 maxGap = static_cast<qint64>(d.sampleIntervalFrames) * maxGapSteps;
            if (gap > maxGap) continue;
            const double overlap = iou(last, d);
            if (overlap + 1e-12 < minIou) continue;
            if (overlap > bestIou + 1e-12) {
                bestIou = overlap;
                bestTrack = i;
            }
        }
        if (bestTrack < 0) {
            Track track;
            track.observations.append(d);
            tracks.append(track);
        } else {
            tracks[bestTrack].observations.append(d);
        }
    }

    QJsonArray result;
    for (const Track &track : tracks) {
        if (track.observations.size() >= minObservations) result.append(trackToJson(track));
    }
    return result;
}

bool registerVibeCutObjectTrackTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("source_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("label"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 256}}},
                                {QStringLiteral("min_score"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                {QStringLiteral("min_iou"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                {QStringLiteral("max_gap_steps"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 20}}},
                                {QStringLiteral("min_observations"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 2}, {QStringLiteral("maximum"), 1000}}}}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_object_tracks");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Derive provenance-safe continuity tracks from repeated sampled-frame object model predictions using same-label geometry overlap. Returns exact observed frames/boxes and explicit unobserved intervals. Tracks are derived predictions, not object/person identity and not continuous observation.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, tool, error);
}
