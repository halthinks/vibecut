/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutroomtone.h"

#include "vibecutmediaevidence.h"
#include "vibecuttoolsurface.h"

#include <QJsonObject>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace {
struct LoudnessObservation {
    QString sourceId;
    QString sourceFingerprint;
    QString extractorId;
    QString extractorVersion;
    int startFrame = -1;
    int endFrame = -1;
    double ptsSeconds = -1.0;
    int sampleIntervalMs = 0;
    double momentaryLufs = -120.691;
    double truePeakDbfs = -200.0;
    bool hasTruePeak = false;
};

struct SilenceRange {
    QString sourceId;
    QString sourceFingerprint;
    int startFrame = -1;
    int endFrame = -1;
};

struct Candidate {
    QString sourceId;
    QString sourceFingerprint;
    QString extractorId;
    QString extractorVersion;
    QList<LoudnessObservation> observations;
};

bool parseLoudness(const QJsonObject &object, LoudnessObservation &result)
{
    if (object.value(QStringLiteral("kind")).toString() != QLatin1String("audio_loudness_sample")) return false;
    result.sourceId = object.value(QStringLiteral("source_id")).toString().trimmed();
    result.sourceFingerprint = object.value(QStringLiteral("source_fingerprint")).toString().trimmed();
    result.extractorId = object.value(QStringLiteral("extractor_id")).toString().trimmed();
    result.extractorVersion = object.value(QStringLiteral("extractor_version")).toString().trimmed();
    result.startFrame = object.value(QStringLiteral("start_frame")).toInt(-1);
    result.endFrame = object.value(QStringLiteral("end_frame")).toInt(-1);
    const QJsonObject metadata = object.value(QStringLiteral("metadata")).toObject();
    result.ptsSeconds = metadata.value(QStringLiteral("sample_pts_seconds")).toDouble(-1.0);
    result.sampleIntervalMs = metadata.value(QStringLiteral("sample_interval_ms")).toInt(0);
    result.momentaryLufs = metadata.value(QStringLiteral("momentary_lufs")).toDouble(-120.691);
    if (metadata.contains(QStringLiteral("true_peak_dbfs"))) {
        result.truePeakDbfs = metadata.value(QStringLiteral("true_peak_dbfs")).toDouble(-200.0);
        result.hasTruePeak = std::isfinite(result.truePeakDbfs);
    }
    return !result.sourceId.isEmpty() && !result.sourceFingerprint.isEmpty() &&
           !result.extractorId.isEmpty() && !result.extractorVersion.isEmpty() &&
           result.startFrame >= 0 && result.endFrame > result.startFrame &&
           result.ptsSeconds >= 0.0 && result.sampleIntervalMs >= 100 &&
           std::isfinite(result.momentaryLufs);
}

bool parseSilence(const QJsonObject &object, SilenceRange &result)
{
    if (object.value(QStringLiteral("kind")).toString() != QLatin1String("silence")) return false;
    result.sourceId = object.value(QStringLiteral("source_id")).toString().trimmed();
    result.sourceFingerprint = object.value(QStringLiteral("source_fingerprint")).toString().trimmed();
    result.startFrame = object.value(QStringLiteral("start_frame")).toInt(-1);
    result.endFrame = object.value(QStringLiteral("end_frame")).toInt(-1);
    return !result.sourceId.isEmpty() && !result.sourceFingerprint.isEmpty() &&
           result.startFrame >= 0 && result.endFrame >= result.startFrame;
}

bool overlapsSilence(const LoudnessObservation &observation, const QList<SilenceRange> &silences)
{
    for (const SilenceRange &silence : silences) {
        if (silence.sourceId != observation.sourceId || silence.sourceFingerprint != observation.sourceFingerprint) continue;
        if (observation.startFrame < silence.endFrame && observation.endFrame > silence.startFrame) return true;
        if (silence.startFrame == silence.endFrame &&
            observation.startFrame <= silence.startFrame && observation.endFrame > silence.startFrame) return true;
    }
    return false;
}

bool sameLineage(const LoudnessObservation &a, const LoudnessObservation &b)
{
    return a.sourceId == b.sourceId && a.sourceFingerprint == b.sourceFingerprint &&
           a.extractorId == b.extractorId && a.extractorVersion == b.extractorVersion;
}

bool consecutive(const LoudnessObservation &a, const LoudnessObservation &b)
{
    if (!sameLineage(a, b) || b.ptsSeconds <= a.ptsSeconds) return false;
    const int intervalMs = qMax(a.sampleIntervalMs, b.sampleIntervalMs);
    const double gapMs = (b.ptsSeconds - a.ptsSeconds) * 1000.0;
    return gapMs <= static_cast<double>(intervalMs) * 1.55 + 1.0;
}

QJsonObject candidateToJson(const Candidate &candidate, double maxSpreadLu)
{
    if (candidate.observations.isEmpty()) return QJsonObject();
    double minLufs = candidate.observations.first().momentaryLufs;
    double maxLufs = minLufs;
    double sumLufs = 0.0;
    double maxTruePeak = -200.0;
    bool hasTruePeak = false;
    QJsonArray frames;
    QJsonArray samples;
    for (const LoudnessObservation &observation : candidate.observations) {
        minLufs = qMin(minLufs, observation.momentaryLufs);
        maxLufs = qMax(maxLufs, observation.momentaryLufs);
        sumLufs += observation.momentaryLufs;
        frames.append(observation.startFrame);
        QJsonObject sample{{QStringLiteral("frame"), observation.startFrame},
                           {QStringLiteral("momentary_lufs"), observation.momentaryLufs}};
        if (observation.hasTruePeak) {
            sample.insert(QStringLiteral("true_peak_dbfs"), observation.truePeakDbfs);
            maxTruePeak = qMax(maxTruePeak, observation.truePeakDbfs);
            hasTruePeak = true;
        }
        samples.append(sample);
    }
    const double spread = maxLufs - minLufs;
    const double stabilityScore = maxSpreadLu > 0.0 ? qBound(0.0, 1.0 - spread / maxSpreadLu, 1.0) : 0.0;
    const LoudnessObservation &first = candidate.observations.first();
    const LoudnessObservation &last = candidate.observations.last();
    QJsonObject result{
        {QStringLiteral("source_id"), candidate.sourceId},
        {QStringLiteral("source_fingerprint"), candidate.sourceFingerprint},
        {QStringLiteral("extractor_id"), candidate.extractorId},
        {QStringLiteral("extractor_version"), candidate.extractorVersion},
        {QStringLiteral("start_frame"), first.startFrame},
        {QStringLiteral("end_frame"), last.endFrame},
        {QStringLiteral("observed_frames"), frames},
        {QStringLiteral("samples"), samples},
        {QStringLiteral("observation_count"), candidate.observations.size()},
        {QStringLiteral("momentary_lufs_mean"), sumLufs / candidate.observations.size()},
        {QStringLiteral("momentary_lufs_min"), minLufs},
        {QStringLiteral("momentary_lufs_max"), maxLufs},
        {QStringLiteral("momentary_spread_lu"), spread},
        {QStringLiteral("stability_score"), stabilityScore},
        {QStringLiteral("authority"), QStringLiteral("derived_candidate")},
        {QStringLiteral("candidate_kind"), QStringLiteral("room_tone")},
        {QStringLiteral("note"), QStringLiteral("Candidate derived from consecutive stable EBU R128 observations that do not intersect stored silence evidence. This is not a semantic assertion that the audio is room tone; audition or richer audio-event evidence is required before promotion.")},
    };
    if (hasTruePeak) result.insert(QStringLiteral("max_true_peak_dbfs"), maxTruePeak);
    return result;
}

QJsonObject roomToneTool(const QJsonObject &input)
{
    QString error;
    const QJsonArray records = VibeCutMediaEvidence::loadCurrent(&error);
    if (!error.isEmpty()) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    const QString sourceId = input.value(QStringLiteral("source_id")).toString().trimmed();
    const double minLufs = qBound(-100.0, input.value(QStringLiteral("min_momentary_lufs")).toDouble(-65.0), 0.0);
    const double maxLufs = qBound(-100.0, input.value(QStringLiteral("max_momentary_lufs")).toDouble(-25.0), 0.0);
    if (minLufs > maxLufs) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("min_momentary_lufs may not exceed max_momentary_lufs.")}};
    const double maxSpread = qBound(0.1, input.value(QStringLiteral("max_spread_lu")).toDouble(5.0), 30.0);
    const int minObservations = qBound(2, input.value(QStringLiteral("min_observations")).toInt(4), 1000);
    const QJsonArray candidates = buildVibeCutRoomToneCandidates(records, sourceId, minLufs, maxLufs, maxSpread, minObservations);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("source_id"), sourceId},
                       {QStringLiteral("candidate_count"), candidates.size()},
                       {QStringLiteral("candidates"), candidates},
                       {QStringLiteral("authority"), QStringLiteral("derived_candidate")}};
}
} // namespace

QJsonArray buildVibeCutRoomToneCandidates(const QJsonArray &records,
                                          const QString &sourceId,
                                          double minMomentaryLufs,
                                          double maxMomentaryLufs,
                                          double maxSpreadLu,
                                          int minObservations)
{
    minMomentaryLufs = qBound(-100.0, minMomentaryLufs, 0.0);
    maxMomentaryLufs = qBound(-100.0, maxMomentaryLufs, 0.0);
    if (minMomentaryLufs > maxMomentaryLufs) return QJsonArray();
    maxSpreadLu = qBound(0.1, maxSpreadLu, 30.0);
    minObservations = qBound(2, minObservations, 1000);

    QList<LoudnessObservation> loudness;
    QList<SilenceRange> silences;
    for (const QJsonValue &value : records) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        LoudnessObservation observation;
        if (parseLoudness(object, observation)) {
            if (sourceId.isEmpty() || observation.sourceId == sourceId) loudness.append(observation);
            continue;
        }
        SilenceRange silence;
        if (parseSilence(object, silence)) {
            if (sourceId.isEmpty() || silence.sourceId == sourceId) silences.append(silence);
        }
    }
    std::sort(loudness.begin(), loudness.end(), [](const LoudnessObservation &a, const LoudnessObservation &b) {
        if (a.sourceId != b.sourceId) return a.sourceId < b.sourceId;
        if (a.sourceFingerprint != b.sourceFingerprint) return a.sourceFingerprint < b.sourceFingerprint;
        if (a.extractorId != b.extractorId) return a.extractorId < b.extractorId;
        if (a.extractorVersion != b.extractorVersion) return a.extractorVersion < b.extractorVersion;
        return a.ptsSeconds < b.ptsSeconds;
    });

    QList<Candidate> runs;
    Candidate current;
    auto flush = [&]() {
        if (current.observations.size() >= minObservations) runs.append(current);
        current = Candidate();
    };
    for (const LoudnessObservation &observation : loudness) {
        const bool eligibleLevel = observation.momentaryLufs >= minMomentaryLufs && observation.momentaryLufs <= maxMomentaryLufs;
        if (!eligibleLevel || overlapsSilence(observation, silences)) {
            flush();
            continue;
        }
        if (!current.observations.isEmpty() && !consecutive(current.observations.last(), observation)) flush();
        if (current.observations.isEmpty()) {
            current.sourceId = observation.sourceId;
            current.sourceFingerprint = observation.sourceFingerprint;
            current.extractorId = observation.extractorId;
            current.extractorVersion = observation.extractorVersion;
        }
        current.observations.append(observation);

        double minSeen = current.observations.first().momentaryLufs;
        double maxSeen = minSeen;
        for (const LoudnessObservation &item : current.observations) {
            minSeen = qMin(minSeen, item.momentaryLufs);
            maxSeen = qMax(maxSeen, item.momentaryLufs);
        }
        if (maxSeen - minSeen > maxSpreadLu) {
            const LoudnessObservation keep = current.observations.takeLast();
            flush();
            current.sourceId = keep.sourceId;
            current.sourceFingerprint = keep.sourceFingerprint;
            current.extractorId = keep.extractorId;
            current.extractorVersion = keep.extractorVersion;
            current.observations.append(keep);
        }
    }
    flush();

    QJsonArray result;
    for (const Candidate &candidate : runs) result.append(candidateToJson(candidate, maxSpreadLu));
    return result;
}

bool registerVibeCutRoomToneTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("source_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("min_momentary_lufs"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), -100.0}, {QStringLiteral("maximum"), 0.0}}},
                                {QStringLiteral("max_momentary_lufs"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), -100.0}, {QStringLiteral("maximum"), 0.0}}},
                                {QStringLiteral("max_spread_lu"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.1}, {QStringLiteral("maximum"), 30.0}}},
                                {QStringLiteral("min_observations"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 2}, {QStringLiteral("maximum"), 1000}}}}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_room_tone_candidates");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Derive reviewable room-tone candidates from consecutive stable audio_r128 measurements while excluding ranges intersecting persisted silence/dead-air evidence. Results are explicitly candidates, retain their observed sample frames, and are never promoted to semantic room-tone fact without audition or richer evidence.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, roomToneTool, error);
}
