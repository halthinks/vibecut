/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutpacing.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "vibecutmediaindex.h"
#include "vibecutroughcutsynthesis.h"
#include "vibecuttoolsurface.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
struct Range {
    int start = -1;
    int end = -1;
};

QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString statFingerprint(const QFileInfo &info)
{
    const QByteArray payload = info.canonicalFilePath().toUtf8() + '\n' + QByteArray::number(info.size()) + '\n' +
                               QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

QJsonObject stats(const QList<int> &values)
{
    if (values.isEmpty()) {
        return QJsonObject{{QStringLiteral("count"), 0}, {QStringLiteral("available"), false}};
    }
    QList<int> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    long double sum = 0.0L;
    for (int value : sorted) sum += value;
    const double mean = static_cast<double>(sum / sorted.size());
    double median = 0.0;
    const int middle = sorted.size() / 2;
    if (sorted.size() % 2 == 0) median = (sorted.at(middle - 1) + sorted.at(middle)) / 2.0;
    else median = sorted.at(middle);
    long double variance = 0.0L;
    for (int value : sorted) {
        const long double delta = static_cast<long double>(value) - mean;
        variance += delta * delta;
    }
    variance /= sorted.size();
    const double stddev = std::sqrt(static_cast<double>(variance));
    return QJsonObject{{QStringLiteral("count"), sorted.size()},
                       {QStringLiteral("available"), true},
                       {QStringLiteral("min_frames"), sorted.first()},
                       {QStringLiteral("max_frames"), sorted.last()},
                       {QStringLiteral("mean_frames"), mean},
                       {QStringLiteral("median_frames"), median},
                       {QStringLiteral("stddev_frames"), stddev},
                       {QStringLiteral("coefficient_of_variation"), mean > 0.0 ? stddev / mean : 0.0}};
}

QList<Range> mergeRanges(QList<Range> ranges)
{
    ranges.erase(std::remove_if(ranges.begin(), ranges.end(), [](const Range &range) {
        return range.start < 0 || range.end <= range.start;
    }), ranges.end());
    std::sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) {
        if (a.start != b.start) return a.start < b.start;
        return a.end < b.end;
    });
    QList<Range> merged;
    for (const Range &range : ranges) {
        if (merged.isEmpty() || range.start > merged.last().end) merged.append(range);
        else merged.last().end = qMax(merged.last().end, range.end);
    }
    return merged;
}

qint64 rangeFrames(const QList<Range> &ranges)
{
    qint64 total = 0;
    for (const Range &range : ranges) total += static_cast<qint64>(range.end) - range.start;
    return total;
}

QList<int> positiveGaps(QList<Range> ranges)
{
    std::sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) {
        if (a.start != b.start) return a.start < b.start;
        return a.end < b.end;
    });
    QList<int> gaps;
    int coveredEnd = -1;
    for (const Range &range : ranges) {
        if (range.start < 0 || range.end <= range.start) continue;
        if (coveredEnd >= 0 && range.start > coveredEnd) gaps.append(range.start - coveredEnd);
        coveredEnd = qMax(coveredEnd, range.end);
    }
    return gaps;
}

bool exactInteger(const QJsonObject &input, const QString &key, qint64 minimum, qint64 maximum,
                  qint64 defaultValue, bool required, qint64 &value, QString *error)
{
    if (!input.contains(key)) {
        if (required) {
            if (error) *error = QStringLiteral("%1 is required.").arg(key);
            return false;
        }
        value = defaultValue;
        return true;
    }
    const QJsonValue raw = input.value(key);
    if (!raw.isDouble()) {
        if (error) *error = QStringLiteral("%1 must be an integer.").arg(key);
        return false;
    }
    const double number = raw.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) {
        if (error) *error = QStringLiteral("%1 must be an integer in the supported range %2..%3.")
                               .arg(key).arg(minimum).arg(maximum);
        return false;
    }
    value = static_cast<qint64>(number);
    return true;
}

bool buildCurrentContext(VibeCutToolSurface *surface, int maxCandidates, int maxTextChars,
                         QJsonObject &context, QString *error)
{
    if (error) error->clear();
    if (!surface) {
        if (error) *error = QStringLiteral("Rough-cut pacing requires the VibeCut tool surface.");
        return false;
    }
    VibeCutMediaIndex index;
    QString indexError;
    if (!index.rebuildFromCurrentProject(&indexError)) {
        if (error) *error = indexError;
        return false;
    }
    context = buildVibeCutRoughCutContext(index.documents(), surface->projectRevision(), maxCandidates, maxTextChars, error);
    return !context.isEmpty() && (!error || error->isEmpty());
}

QJsonObject sourceTool(const QJsonObject &input)
{
    if (!pCore) return err(QStringLiteral("Kdenlive core is unavailable."));
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty."));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip || !clip->hasUrl()) return err(QStringLiteral("Pacing analysis requires an existing file-backed bin asset."));
    const QFileInfo info(clip->url());
    if (!info.exists() || !info.isFile()) return err(QStringLiteral("Current source file is unavailable."));
    const QString sourceId = QStringLiteral("bin:%1").arg(binId);
    const QString fingerprint = statFingerprint(info);

    QString loadError;
    const QJsonArray raw = VibeCutMediaEvidence::loadCurrent(&loadError);
    if (!loadError.isEmpty()) return err(loadError);
    QList<VibeCutMediaEvidenceRecord> records;
    for (const QJsonValue &value : raw) {
        if (!value.isObject()) continue;
        VibeCutMediaEvidenceRecord record;
        QString recordError;
        if (!VibeCutMediaEvidenceRecord::fromJson(value.toObject(), record, &recordError)) return err(recordError);
        records.append(record);
    }
    QString analysisError;
    QJsonObject result = analyzeVibeCutSourcePacing(records, sourceId, fingerprint, &analysisError);
    if (!analysisError.isEmpty()) return err(analysisError);
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("bin_id"), binId);
    const double fps = pCore->getCurrentFps();
    if (std::isfinite(fps) && fps > 0.0) {
        result.insert(QStringLiteral("project_fps"), fps);
        result.insert(QStringLiteral("frame_domain_note"), QStringLiteral("Frame-domain evidence in this project was admitted using the project frame-rate context; seconds may be derived by dividing frame values by project_fps where appropriate."));
    }
    return result;
}

QJsonObject roughCutTool(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface) return err(QStringLiteral("Rough-cut pacing requires the VibeCut tool surface."));
    QString numberError;
    qint64 baseRevision = -1, maxCandidates = 200, maxTextChars = 600;
    constexpr qint64 MaxExactJsonInteger = 9007199254740991LL;
    if (!exactInteger(input, QStringLiteral("base_revision"), 0, MaxExactJsonInteger, -1, true, baseRevision, &numberError) ||
        !exactInteger(input, QStringLiteral("context_max_candidates"), 1, 300, 200, true, maxCandidates, &numberError) ||
        !exactInteger(input, QStringLiteral("context_max_text_chars"), 64, 2048, 600, true, maxTextChars, &numberError)) {
        return err(numberError);
    }
    if (static_cast<quint64>(baseRevision) != surface->projectRevision()) return err(QStringLiteral("Rough-cut pacing context is stale."));
    const QString contextSha = input.value(QStringLiteral("context_sha256")).toString().trimmed();
    if (contextSha.size() != 64) return err(QStringLiteral("context_sha256 must contain exactly 64 characters."));
    if (!input.value(QStringLiteral("selected_candidate_ids")).isArray()) return err(QStringLiteral("selected_candidate_ids must be an array."));

    QJsonObject context;
    QString contextError;
    if (!buildCurrentContext(surface, static_cast<int>(maxCandidates), static_cast<int>(maxTextChars), context, &contextError)) return err(contextError);
    if (context.value(QStringLiteral("context_sha256")).toString() != contextSha) return err(QStringLiteral("Rough-cut pacing was not requested against the exact current candidate context."));
    QString analysisError;
    QJsonObject result = analyzeVibeCutRoughCutPacing(context, input.value(QStringLiteral("selected_candidate_ids")).toArray(),
                                                      surface->projectRevision(), &analysisError);
    if (!analysisError.isEmpty()) return err(analysisError);
    result.insert(QStringLiteral("ok"), true);
    return result;
}
} // namespace

QJsonObject analyzeVibeCutSourcePacing(const QList<VibeCutMediaEvidenceRecord> &records,
                                       const QString &sourceId,
                                       const QString &sourceFingerprint,
                                       QString *error)
{
    if (error) error->clear();
    const QString wantedSource = sourceId.trimmed();
    const QString wantedFingerprint = sourceFingerprint.trimmed();
    if (wantedSource.isEmpty() || wantedFingerprint.isEmpty()) {
        if (error) *error = QStringLiteral("Source pacing requires exact source id and fingerprint.");
        return {};
    }

    QList<int> shotDurations;
    QList<int> transcriptDurations;
    QList<Range> transcriptRanges;
    QList<Range> silenceRanges;
    QHash<QString, qint64> speakerFrames;
    int speakerSegmentCount = 0;
    int relevantRecordCount = 0;
    int extentStart = std::numeric_limits<int>::max();
    int extentEnd = -1;
    QHash<QString, int> kindCounts;

    for (const VibeCutMediaEvidenceRecord &record : records) {
        if (record.sourceId != wantedSource || record.sourceFingerprint != wantedFingerprint) continue;
        if (record.startFrame < 0 || record.endFrame < record.startFrame) continue;
        ++relevantRecordCount;
        kindCounts[record.kind] += 1;
        if (record.endFrame > record.startFrame) {
            extentStart = qMin(extentStart, record.startFrame);
            extentEnd = qMax(extentEnd, record.endFrame);
        }
        const int duration = qMax(0, record.endFrame - record.startFrame);
        if (record.kind == QLatin1String("shot_segment") && duration > 0) shotDurations.append(duration);
        else if (record.kind == QLatin1String("silence") && duration > 0) silenceRanges.append({record.startFrame, record.endFrame});
        else if (record.kind == QLatin1String("transcript_segment") && duration > 0) {
            transcriptDurations.append(duration);
            transcriptRanges.append({record.startFrame, record.endFrame});
        } else if (record.kind == QLatin1String("speaker_segment") && duration > 0) {
            const QString cluster = record.metadata.value(QStringLiteral("speaker_cluster_id")).toString().trimmed();
            if (!cluster.isEmpty()) speakerFrames[cluster] += duration;
            ++speakerSegmentCount;
        }
    }
    if (relevantRecordCount == 0) {
        if (error) *error = QStringLiteral("No current persisted pacing evidence exists for this source fingerprint.");
        return {};
    }

    const QList<Range> mergedSilence = mergeRanges(silenceRanges);
    const qint64 silenceFrames = rangeFrames(mergedSilence);
    const QList<int> transcriptGaps = positiveGaps(transcriptRanges);
    const qint64 extentFrames = (extentStart != std::numeric_limits<int>::max() && extentEnd > extentStart)
                                  ? static_cast<qint64>(extentEnd) - extentStart : 0;
    qint64 rawSpeakerFrames = 0;
    qint64 dominantSpeakerFrames = 0;
    QString dominantCluster;
    for (auto it = speakerFrames.constBegin(); it != speakerFrames.constEnd(); ++it) {
        rawSpeakerFrames += it.value();
        if (it.value() > dominantSpeakerFrames || (it.value() == dominantSpeakerFrames && it.key() < dominantCluster)) {
            dominantSpeakerFrames = it.value();
            dominantCluster = it.key();
        }
    }

    QJsonObject counts;
    QStringList kinds = kindCounts.keys();
    std::sort(kinds.begin(), kinds.end());
    for (const QString &kind : kinds) counts.insert(kind, kindCounts.value(kind));

    QJsonObject speakerMetrics{{QStringLiteral("segment_count"), speakerSegmentCount},
                               {QStringLiteral("cluster_count"), speakerFrames.size()},
                               {QStringLiteral("raw_cluster_coverage_frames"), rawSpeakerFrames},
                               {QStringLiteral("dominant_cluster_id"), dominantCluster},
                               {QStringLiteral("dominant_cluster_raw_frames"), dominantSpeakerFrames},
                               {QStringLiteral("dominant_share_of_raw_speaker_frames"), rawSpeakerFrames > 0 ? static_cast<double>(dominantSpeakerFrames) / rawSpeakerFrames : -1.0},
                               {QStringLiteral("overlap_note"), QStringLiteral("Speaker durations are raw segment coverage and may overlap; the dominant share denominator is raw speaker-segment frames, not unique timeline coverage.")}};

    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("authority"), QStringLiteral("derived_analysis")},
                       {QStringLiteral("analysis_semantics"), QStringLiteral("descriptive_frame_domain_pacing_measurements_not_quality_judgment")},
                       {QStringLiteral("source_id"), wantedSource},
                       {QStringLiteral("source_fingerprint"), wantedFingerprint},
                       {QStringLiteral("relevant_record_count"), relevantRecordCount},
                       {QStringLiteral("evidence_kind_counts"), counts},
                       {QStringLiteral("observed_extent_start_frame"), extentFrames > 0 ? extentStart : -1},
                       {QStringLiteral("observed_extent_end_frame"), extentFrames > 0 ? extentEnd : -1},
                       {QStringLiteral("observed_extent_frames"), extentFrames},
                       {QStringLiteral("shot_duration_metrics"), stats(shotDurations)},
                       {QStringLiteral("transcript_segment_duration_metrics"), stats(transcriptDurations)},
                       {QStringLiteral("transcript_positive_gap_metrics"), stats(transcriptGaps)},
                       {QStringLiteral("silence_metrics"), QJsonObject{{QStringLiteral("raw_range_count"), silenceRanges.size()},
                                                                       {QStringLiteral("merged_range_count"), mergedSilence.size()},
                                                                       {QStringLiteral("merged_silence_frames"), silenceFrames},
                                                                       {QStringLiteral("coverage_of_observed_extent"), extentFrames > 0 ? static_cast<double>(silenceFrames) / extentFrames : -1.0}}},
                       {QStringLiteral("speaker_metrics"), speakerMetrics},
                       {QStringLiteral("normative_thresholds_applied"), false}};
}

QJsonObject analyzeVibeCutRoughCutPacing(const QJsonObject &context,
                                         const QJsonArray &selectedCandidateIds,
                                         quint64 currentRevision,
                                         QString *error)
{
    if (error) error->clear();
    const QJsonObject proposal{{QStringLiteral("schema_version"), 1},
                               {QStringLiteral("authority"), QStringLiteral("proposal")},
                               {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                               {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                               {QStringLiteral("objective"), QStringLiteral("Measure pacing of selected rough-cut candidates")},
                               {QStringLiteral("selected_candidate_ids"), selectedCandidateIds}};
    QString proposalError;
    const QJsonObject validated = validateVibeCutRoughCutProposal(context, proposal, currentRevision, &proposalError);
    if (!proposalError.isEmpty()) {
        if (error) *error = proposalError;
        return {};
    }

    QList<int> durations;
    QList<int> durationDeltas;
    QJsonArray rhythm;
    int truncatedTextCount = 0;
    qint64 previewChars = 0;
    int previousDuration = -1;
    for (const QJsonValue &value : validated.value(QStringLiteral("segments")).toArray()) {
        const QJsonObject segment = value.toObject();
        const int duration = segment.value(QStringLiteral("duration_frames")).toInt(-1);
        if (duration <= 0) continue;
        durations.append(duration);
        if (previousDuration > 0) durationDeltas.append(qAbs(duration - previousDuration));
        previousDuration = duration;
        const QString text = segment.value(QStringLiteral("text")).toString();
        previewChars += text.size();
        if (segment.value(QStringLiteral("text_truncated")).toBool(false)) ++truncatedTextCount;
        rhythm.append(QJsonObject{{QStringLiteral("candidate_id"), segment.value(QStringLiteral("candidate_id"))},
                                  {QStringLiteral("proposal_order"), segment.value(QStringLiteral("proposal_order"))},
                                  {QStringLiteral("duration_frames"), duration},
                                  {QStringLiteral("preview_text_chars"), text.size()},
                                  {QStringLiteral("text_truncated"), segment.value(QStringLiteral("text_truncated"))}});
    }
    if (durations.isEmpty()) {
        if (error) *error = QStringLiteral("Validated rough-cut selection contains no positive-duration segments.");
        return {};
    }
    qint64 contextDuration = 0;
    for (const QJsonValue &value : context.value(QStringLiteral("candidates")).toArray()) {
        contextDuration += qMax(0, value.toObject().value(QStringLiteral("duration_frames")).toInt(0));
    }
    const qint64 selectedFrames = validated.value(QStringLiteral("total_frames")).toInteger(0);
    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("authority"), QStringLiteral("derived_analysis")},
                       {QStringLiteral("analysis_semantics"), QStringLiteral("descriptive_rough_cut_rhythm_not_quality_judgment")},
                       {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                       {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                       {QStringLiteral("segment_count"), durations.size()},
                       {QStringLiteral("total_frames"), selectedFrames},
                       {QStringLiteral("duration_metrics"), stats(durations)},
                       {QStringLiteral("consecutive_duration_delta_metrics"), stats(durationDeltas)},
                       {QStringLiteral("reorders_source_chronology"), validated.value(QStringLiteral("reorders_timeline"))},
                       {QStringLiteral("overlap_warning_count"), validated.value(QStringLiteral("overlap_warning_count"))},
                       {QStringLiteral("selected_raw_duration_share_of_context"), contextDuration > 0 ? static_cast<double>(selectedFrames) / contextDuration : -1.0},
                       {QStringLiteral("preview_text_characters"), previewChars},
                       {QStringLiteral("preview_characters_per_selected_frame"), selectedFrames > 0 ? static_cast<double>(previewChars) / selectedFrames : -1.0},
                       {QStringLiteral("text_density_basis"), QStringLiteral("bounded_context_preview_characters_per_frame")},
                       {QStringLiteral("truncated_text_segment_count"), truncatedTextCount},
                       {QStringLiteral("rhythm_segments"), rhythm},
                       {QStringLiteral("normative_thresholds_applied"), false},
                       {QStringLiteral("executable"), false},
                       {QStringLiteral("mutation_authority"), QStringLiteral("none")}};
}

bool registerVibeCutPacingTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject sourceInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{{QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                                  {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy sourcePolicy;
    sourcePolicy.name = QStringLiteral("media_source_pacing");
    sourcePolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), sourcePolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Analyze current persisted shot, silence, transcript and speaker evidence for one current file-backed bin source using descriptive frame-domain pacing statistics only. No good/bad threshold or edit authority is applied.")},
                                          {QStringLiteral("input_schema"), sourceInput}},
                              sourcePolicy, sourceTool, error)) return false;

    VibeCutToolSurface *surfacePtr = &surface;
    const QJsonObject roughInput{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), QJsonObject{
                                     {QStringLiteral("base_revision"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                     {QStringLiteral("context_sha256"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 64}, {QStringLiteral("maxLength"), 64}}},
                                     {QStringLiteral("context_max_candidates"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 300}}},
                                     {QStringLiteral("context_max_text_chars"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 64}, {QStringLiteral("maximum"), 2048}}},
                                     {QStringLiteral("selected_candidate_ids"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                                             {QStringLiteral("minItems"), 1}, {QStringLiteral("maxItems"), 100},
                                                                                             {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}}}},
                                 {QStringLiteral("required"), QJsonArray{QStringLiteral("base_revision"), QStringLiteral("context_sha256"),
                                                                         QStringLiteral("context_max_candidates"), QStringLiteral("context_max_text_chars"),
                                                                         QStringLiteral("selected_candidate_ids")}},
                                 {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy roughPolicy;
    roughPolicy.name = QStringLiteral("rough_cut_pacing_analyze");
    roughPolicy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), roughPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Measure one exact current rough-cut candidate-ID sequence using descriptive duration/rhythm, chronology, overlap and bounded transcript-density statistics. Output is derived analysis only and cannot execute edits.")},
                                            {QStringLiteral("input_schema"), roughInput}},
                                roughPolicy, [surfacePtr](const QJsonObject &input) { return roughCutTool(surfacePtr, input); }, error);
}
