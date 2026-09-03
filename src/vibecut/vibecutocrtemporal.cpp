/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutocrtemporal.h"

#include "vibecutmediaevidence.h"
#include "vibecuttoolsurface.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

#include <algorithm>
#include <limits>

namespace {
struct Observation {
    QString sourceId;
    QString sourceFingerprint;
    QString extractorId;
    QString extractorVersion;
    QString text;
    QString normalized;
    int frame = -1;
    int interval = 1;
    double confidence = 0.0;
    int imageWidth = 0;
    int imageHeight = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct Track {
    QString sourceId;
    QString sourceFingerprint;
    QString extractorId;
    QString extractorVersion;
    QString representativeText;
    QString representativeNormalized;
    double representativeConfidence = -1.0;
    double confidenceSum = 0.0;
    double confidenceMin = 1.0;
    double confidenceMax = 0.0;
    int firstFrame = -1;
    int lastFrame = -1;
    int lastInterval = 1;
    int imageWidth = 0;
    int imageHeight = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    QList<int> observedFrames;
    int observations = 0;
};

QString normalizeText(const QString &text)
{
    QString normalized = text.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" "));
    return normalized.simplified();
}

QSet<QString> tokenSet(const QString &text)
{
    QSet<QString> result;
    for (const QString &token : text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) result.insert(token);
    return result;
}

QSet<QString> bigramSet(const QString &text)
{
    QSet<QString> result;
    const QString compact = QString(text).remove(QLatin1Char(' '));
    if (compact.size() < 2) {
        if (!compact.isEmpty()) result.insert(compact);
        return result;
    }
    for (int i = 0; i + 1 < compact.size(); ++i) result.insert(compact.mid(i, 2));
    return result;
}

double dice(const QSet<QString> &a, const QSet<QString> &b)
{
    if (a.isEmpty() && b.isEmpty()) return 1.0;
    if (a.isEmpty() || b.isEmpty()) return 0.0;
    int intersection = 0;
    for (const QString &value : a) if (b.contains(value)) ++intersection;
    return (2.0 * intersection) / static_cast<double>(a.size() + b.size());
}

double textSimilarity(const QString &a, const QString &b)
{
    if (a == b) return 1.0;
    if (a.isEmpty() || b.isEmpty()) return 0.0;
    return qMax(dice(tokenSet(a), tokenSet(b)), dice(bigramSet(a), bigramSet(b)));
}

double boxIou(const Track &track, const Observation &observation)
{
    if (track.imageWidth <= 0 || track.imageHeight <= 0 || observation.imageWidth <= 0 || observation.imageHeight <= 0) return 0.0;

    // Compare normalized coordinates so resolution changes do not destroy a
    // temporal match. Scale into a fixed million-unit plane before integer
    // intersection arithmetic.
    constexpr qint64 Scale = 1000000;
    auto nx = [](int value, int total) -> qint64 { return total > 0 ? (static_cast<qint64>(value) * Scale) / total : 0; };
    const qint64 ax1 = nx(track.x, track.imageWidth);
    const qint64 ay1 = nx(track.y, track.imageHeight);
    const qint64 ax2 = nx(track.x + track.width, track.imageWidth);
    const qint64 ay2 = nx(track.y + track.height, track.imageHeight);
    const qint64 bx1 = nx(observation.x, observation.imageWidth);
    const qint64 by1 = nx(observation.y, observation.imageHeight);
    const qint64 bx2 = nx(observation.x + observation.width, observation.imageWidth);
    const qint64 by2 = nx(observation.y + observation.height, observation.imageHeight);
    const qint64 ix1 = qMax(ax1, bx1);
    const qint64 iy1 = qMax(ay1, by1);
    const qint64 ix2 = qMin(ax2, bx2);
    const qint64 iy2 = qMin(ay2, by2);
    if (ix2 <= ix1 || iy2 <= iy1) return 0.0;
    const long double intersection = static_cast<long double>(ix2 - ix1) * static_cast<long double>(iy2 - iy1);
    const long double areaA = static_cast<long double>(ax2 - ax1) * static_cast<long double>(ay2 - ay1);
    const long double areaB = static_cast<long double>(bx2 - bx1) * static_cast<long double>(by2 - by1);
    const long double denominator = areaA + areaB - intersection;
    return denominator > 0.0L ? static_cast<double>(intersection / denominator) : 0.0;
}

bool sameEvidenceLineage(const Track &track, const Observation &observation)
{
    return track.sourceId == observation.sourceId &&
           track.sourceFingerprint == observation.sourceFingerprint &&
           track.extractorId == observation.extractorId &&
           track.extractorVersion == observation.extractorVersion;
}

bool parseObservation(const QJsonObject &object, Observation &result)
{
    if (object.value(QStringLiteral("kind")).toString() != QLatin1String("ocr_text")) return false;
    result.sourceId = object.value(QStringLiteral("source_id")).toString().trimmed();
    result.sourceFingerprint = object.value(QStringLiteral("source_fingerprint")).toString().trimmed();
    result.extractorId = object.value(QStringLiteral("extractor_id")).toString().trimmed();
    result.extractorVersion = object.value(QStringLiteral("extractor_version")).toString().trimmed();
    result.text = object.value(QStringLiteral("text")).toString().trimmed();
    result.normalized = normalizeText(result.text);
    result.frame = object.value(QStringLiteral("start_frame")).toInt(-1);
    result.confidence = object.value(QStringLiteral("confidence")).toDouble(-1.0);
    const QJsonObject metadata = object.value(QStringLiteral("metadata")).toObject();
    result.interval = qMax(1, metadata.value(QStringLiteral("sample_interval_frames")).toInt(1));
    result.imageWidth = metadata.value(QStringLiteral("image_width")).toInt(-1);
    result.imageHeight = metadata.value(QStringLiteral("image_height")).toInt(-1);
    const QJsonObject box = metadata.value(QStringLiteral("bbox_pixels")).toObject();
    result.x = box.value(QStringLiteral("x")).toInt(-1);
    result.y = box.value(QStringLiteral("y")).toInt(-1);
    result.width = box.value(QStringLiteral("width")).toInt(-1);
    result.height = box.value(QStringLiteral("height")).toInt(-1);
    return !result.sourceId.isEmpty() && !result.sourceFingerprint.isEmpty() && !result.extractorId.isEmpty() &&
           !result.extractorVersion.isEmpty() && !result.normalized.isEmpty() && result.frame >= 0 &&
           result.confidence >= 0.0 && result.confidence <= 1.0 && result.imageWidth > 0 && result.imageHeight > 0 &&
           result.x >= 0 && result.y >= 0 && result.width > 0 && result.height > 0;
}

void appendObservation(Track &track, const Observation &observation)
{
    if (track.observations == 0) {
        track.sourceId = observation.sourceId;
        track.sourceFingerprint = observation.sourceFingerprint;
        track.extractorId = observation.extractorId;
        track.extractorVersion = observation.extractorVersion;
        track.firstFrame = observation.frame;
    }
    track.lastFrame = observation.frame;
    track.lastInterval = observation.interval;
    track.imageWidth = observation.imageWidth;
    track.imageHeight = observation.imageHeight;
    track.x = observation.x;
    track.y = observation.y;
    track.width = observation.width;
    track.height = observation.height;
    track.observedFrames.append(observation.frame);
    ++track.observations;
    track.confidenceSum += observation.confidence;
    track.confidenceMin = qMin(track.confidenceMin, observation.confidence);
    track.confidenceMax = qMax(track.confidenceMax, observation.confidence);
    if (observation.confidence > track.representativeConfidence) {
        track.representativeConfidence = observation.confidence;
        track.representativeText = observation.text;
        track.representativeNormalized = observation.normalized;
    }
}

QJsonObject trackToJson(const Track &track)
{
    QJsonArray observed;
    for (int frame : track.observedFrames) observed.append(frame);
    const int windowFrames = track.lastFrame >= track.firstFrame ? track.lastFrame - track.firstFrame + 1 : 0;
    const int unobserved = qMax(0, windowFrames - track.observedFrames.size());
    return QJsonObject{
        {QStringLiteral("source_id"), track.sourceId},
        {QStringLiteral("source_fingerprint"), track.sourceFingerprint},
        {QStringLiteral("extractor_id"), track.extractorId},
        {QStringLiteral("extractor_version"), track.extractorVersion},
        {QStringLiteral("text"), track.representativeText},
        {QStringLiteral("first_observed_frame"), track.firstFrame},
        {QStringLiteral("last_observed_frame"), track.lastFrame},
        {QStringLiteral("inferred_window_start_frame"), track.firstFrame},
        {QStringLiteral("inferred_window_end_frame"), track.lastFrame + 1},
        {QStringLiteral("observed_frames"), observed},
        {QStringLiteral("observation_count"), track.observations},
        {QStringLiteral("unobserved_gap_frames"), unobserved},
        {QStringLiteral("confidence_mean"), track.observations > 0 ? track.confidenceSum / track.observations : 0.0},
        {QStringLiteral("confidence_min"), track.observations > 0 ? track.confidenceMin : 0.0},
        {QStringLiteral("confidence_max"), track.confidenceMax},
        {QStringLiteral("representative_bbox_pixels"), QJsonObject{{QStringLiteral("x"), track.x}, {QStringLiteral("y"), track.y},
                                                                     {QStringLiteral("width"), track.width}, {QStringLiteral("height"), track.height}}},
        {QStringLiteral("representative_image_width"), track.imageWidth},
        {QStringLiteral("representative_image_height"), track.imageHeight},
        {QStringLiteral("authority"), QStringLiteral("inferred_temporal_track")},
        {QStringLiteral("fully_observed"), track.observedFrames.size() == windowFrames},
        {QStringLiteral("note"), QStringLiteral("Only observed_frames are direct OCR observations. The window between them is an inference from recurring text/geometry and must not be promoted to per-frame observation without denser evidence.")},
    };
}

QJsonObject buildTracksTool(const QJsonObject &input)
{
    QString error;
    QJsonArray records = VibeCutMediaEvidence::loadCurrent(&error);
    if (!error.isEmpty()) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    const QString sourceId = input.value(QStringLiteral("source_id")).toString().trimmed();
    if (!sourceId.isEmpty()) {
        QJsonArray filtered;
        for (const QJsonValue &value : records) {
            const QJsonObject object = value.toObject();
            if (object.value(QStringLiteral("source_id")).toString() == sourceId) filtered.append(object);
        }
        records = filtered;
    }
    const double minTextSimilarity = qBound(0.0, input.value(QStringLiteral("min_text_similarity")).toDouble(0.85), 1.0);
    const double minBoxIou = qBound(0.0, input.value(QStringLiteral("min_box_iou")).toDouble(0.25), 1.0);
    const int maxMissingSamples = qBound(0, input.value(QStringLiteral("max_missing_samples")).toInt(0), 20);
    const int minObservations = qBound(1, input.value(QStringLiteral("min_observations")).toInt(2), 1000);
    const QJsonArray tracks = buildVibeCutOcrTemporalTracks(records, minTextSimilarity, minBoxIou, maxMissingSamples, minObservations);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("source_id"), sourceId},
                       {QStringLiteral("track_count"), tracks.size()},
                       {QStringLiteral("tracks"), tracks},
                       {QStringLiteral("authority"), QStringLiteral("derived_inference_from_sampled_ocr")}};
}
} // namespace

QJsonArray buildVibeCutOcrTemporalTracks(const QJsonArray &records,
                                         double minTextSimilarity,
                                         double minBoxIou,
                                         int maxMissingSamples,
                                         int minObservations)
{
    minTextSimilarity = qBound(0.0, minTextSimilarity, 1.0);
    minBoxIou = qBound(0.0, minBoxIou, 1.0);
    maxMissingSamples = qBound(0, maxMissingSamples, 20);
    minObservations = qBound(1, minObservations, 1000);

    QList<Observation> observations;
    for (const QJsonValue &value : records) {
        if (!value.isObject()) continue;
        Observation observation;
        if (parseObservation(value.toObject(), observation)) observations.append(observation);
    }
    std::sort(observations.begin(), observations.end(), [](const Observation &a, const Observation &b) {
        if (a.sourceId != b.sourceId) return a.sourceId < b.sourceId;
        if (a.sourceFingerprint != b.sourceFingerprint) return a.sourceFingerprint < b.sourceFingerprint;
        if (a.extractorId != b.extractorId) return a.extractorId < b.extractorId;
        if (a.extractorVersion != b.extractorVersion) return a.extractorVersion < b.extractorVersion;
        if (a.frame != b.frame) return a.frame < b.frame;
        return a.normalized < b.normalized;
    });

    QList<Track> tracks;
    for (const Observation &observation : observations) {
        int bestIndex = -1;
        double bestScore = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < tracks.size(); ++i) {
            const Track &track = tracks.at(i);
            if (!sameEvidenceLineage(track, observation) || track.lastFrame < 0 || observation.frame <= track.lastFrame) continue;
            const int allowedGap = qMax(track.lastInterval, observation.interval) * (maxMissingSamples + 1);
            if (observation.frame - track.lastFrame > allowedGap) continue;
            const double textScore = textSimilarity(track.representativeNormalized, observation.normalized);
            if (textScore < minTextSimilarity) continue;
            const double geometryScore = boxIou(track, observation);
            if (geometryScore < minBoxIou) continue;
            const double score = textScore * 0.75 + geometryScore * 0.25;
            if (score > bestScore) {
                bestScore = score;
                bestIndex = i;
            }
        }
        if (bestIndex < 0) {
            Track track;
            appendObservation(track, observation);
            tracks.append(track);
        } else {
            appendObservation(tracks[bestIndex], observation);
        }
    }

    QJsonArray result;
    for (const Track &track : tracks) {
        if (track.observations < minObservations) continue;
        result.append(trackToJson(track));
    }
    return result;
}

bool registerVibeCutOcrTemporalTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("source_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("min_text_similarity"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                {QStringLiteral("min_box_iou"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                {QStringLiteral("max_missing_samples"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 20}}},
                                {QStringLiteral("min_observations"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 1000}}}}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_ocr_tracks");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Consolidate persisted one-frame OCR observations into read-only recurring-text tracks using text similarity, normalized box overlap and bounded sample gaps. Output explicitly distinguishes observed_frames from an inferred temporal window; it does not write evidence or claim unsampled frames were observed.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, buildTracksTool, error);
}
