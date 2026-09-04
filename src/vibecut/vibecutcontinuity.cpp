/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutcontinuity.h"

#include "vibecutmediaindex.h"
#include "vibecutroughcutsynthesis.h"
#include "vibecuttoolsurface.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

bool exactInteger(const QJsonObject &input, const QString &key, qint64 minimum, qint64 maximum,
                  qint64 &value, QString *error)
{
    if (!input.contains(key) || !input.value(key).isDouble()) {
        if (error) *error = QStringLiteral("%1 must be an integer.").arg(key);
        return false;
    }
    const double number = input.value(key).toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) {
        if (error) *error = QStringLiteral("%1 must be an integer in the supported range %2..%3.")
                               .arg(key).arg(minimum).arg(maximum);
        return false;
    }
    value = static_cast<qint64>(number);
    return true;
}

QString frameDomain(const QJsonObject &candidate)
{
    const QString source = candidate.value(QStringLiteral("source_id")).toString().trimmed();
    const QString fingerprint = candidate.value(QStringLiteral("source_fingerprint")).toString().trimmed();
    if (source.isEmpty()) return QStringLiteral("active_timeline_unscoped");
    if (source.startsWith(QStringLiteral("timeline:"))) {
        const QStringList parts = source.split(QLatin1Char(':'));
        const QString timelineId = parts.size() >= 2 ? parts.at(1) : source;
        return QStringLiteral("timeline:%1|%2").arg(timelineId, fingerprint);
    }
    return source + QLatin1Char('|') + fingerprint;
}

bool buildCurrentContext(VibeCutToolSurface *surface, int maxCandidates, int maxTextChars,
                         QJsonObject &context, QString *error)
{
    if (error) error->clear();
    if (!surface) {
        if (error) *error = QStringLiteral("Rough-cut continuity requires the VibeCut tool surface.");
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

QJsonObject toolHandler(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface) return err(QStringLiteral("Rough-cut continuity requires the VibeCut tool surface."));
    constexpr qint64 MaxExactJsonInteger = 9007199254740991LL;
    qint64 baseRevision = -1;
    qint64 maxCandidates = 0;
    qint64 maxTextChars = 0;
    QString numberError;
    if (!exactInteger(input, QStringLiteral("base_revision"), 0, MaxExactJsonInteger, baseRevision, &numberError) ||
        !exactInteger(input, QStringLiteral("context_max_candidates"), 1, 300, maxCandidates, &numberError) ||
        !exactInteger(input, QStringLiteral("context_max_text_chars"), 64, 2048, maxTextChars, &numberError)) {
        return err(numberError);
    }
    if (static_cast<quint64>(baseRevision) != surface->projectRevision()) {
        return err(QStringLiteral("Rough-cut continuity context is stale."));
    }
    const QString contextSha = input.value(QStringLiteral("context_sha256")).toString().trimmed();
    if (contextSha.size() != 64) return err(QStringLiteral("context_sha256 must contain exactly 64 characters."));
    if (!input.value(QStringLiteral("selected_candidate_ids")).isArray()) {
        return err(QStringLiteral("selected_candidate_ids must be an array."));
    }

    QJsonObject context;
    QString contextError;
    if (!buildCurrentContext(surface, static_cast<int>(maxCandidates), static_cast<int>(maxTextChars), context, &contextError)) {
        return err(contextError);
    }
    if (context.value(QStringLiteral("context_sha256")).toString() != contextSha) {
        return err(QStringLiteral("Rough-cut continuity was not requested against the exact current candidate context."));
    }
    QString analysisError;
    QJsonObject result = analyzeVibeCutRoughCutContinuity(context,
                                                          input.value(QStringLiteral("selected_candidate_ids")).toArray(),
                                                          surface->projectRevision(), &analysisError);
    if (!analysisError.isEmpty()) return err(analysisError);
    result.insert(QStringLiteral("ok"), true);
    return result;
}
} // namespace

QJsonObject analyzeVibeCutRoughCutContinuity(const QJsonObject &context,
                                             const QJsonArray &selectedCandidateIds,
                                             quint64 currentRevision,
                                             QString *error)
{
    if (error) error->clear();
    const QJsonObject proposal{{QStringLiteral("schema_version"), 1},
                               {QStringLiteral("authority"), QStringLiteral("proposal")},
                               {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                               {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                               {QStringLiteral("objective"), QStringLiteral("Analyze structural continuity of selected rough-cut candidates")},
                               {QStringLiteral("selected_candidate_ids"), selectedCandidateIds}};
    QString proposalError;
    const QJsonObject validated = validateVibeCutRoughCutProposal(context, proposal, currentRevision, &proposalError);
    if (!proposalError.isEmpty()) {
        if (error) *error = proposalError;
        return {};
    }

    const QJsonArray segments = validated.value(QStringLiteral("segments")).toArray();
    if (segments.isEmpty()) {
        if (error) *error = QStringLiteral("Validated rough-cut selection contains no segments.");
        return {};
    }

    QJsonArray warnings;
    QList<QJsonObject> sourceGaps;
    QHash<QString, QString> firstByTextHash;
    int chronologyReversalCount = 0;
    int overlapCount = 0;
    int repeatedTextCount = 0;
    int provenanceChangeCount = 0;
    int frameComparisonSkippedCount = 0;

    for (int i = 0; i < segments.size(); ++i) {
        const QJsonObject current = segments.at(i).toObject();
        const QString currentId = current.value(QStringLiteral("candidate_id")).toString();
        const QString textHash = current.value(QStringLiteral("text_sha256")).toString();
        if (!textHash.isEmpty()) {
            if (firstByTextHash.contains(textHash)) {
                warnings.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("repeated_transcript_content")},
                                            {QStringLiteral("candidate_id"), currentId},
                                            {QStringLiteral("first_matching_candidate_id"), firstByTextHash.value(textHash)},
                                            {QStringLiteral("text_sha256"), textHash},
                                            {QStringLiteral("basis"), QStringLiteral("exact_full_normalized_text_sha256")},
                                            {QStringLiteral("interpretation"), QStringLiteral("review_candidate_not_error")}});
                ++repeatedTextCount;
            } else {
                firstByTextHash.insert(textHash, currentId);
            }
        }
        if (i == 0) continue;

        const QJsonObject previous = segments.at(i - 1).toObject();
        const QString previousId = previous.value(QStringLiteral("candidate_id")).toString();
        const int previousStart = previous.value(QStringLiteral("start_frame")).toInt(-1);
        const int previousEnd = previous.value(QStringLiteral("end_frame")).toInt(-1);
        const int currentStart = current.value(QStringLiteral("start_frame")).toInt(-1);
        const int currentEnd = current.value(QStringLiteral("end_frame")).toInt(-1);
        const QString previousDomain = frameDomain(previous);
        const QString currentDomain = frameDomain(current);
        const bool comparableFrames = !previousDomain.isEmpty() && previousDomain == currentDomain;
        const QJsonObject edgeBase{{QStringLiteral("proposal_edge_index"), i - 1},
                                   {QStringLiteral("left_candidate_id"), previousId},
                                   {QStringLiteral("right_candidate_id"), currentId},
                                   {QStringLiteral("left_frame_domain"), previousDomain},
                                   {QStringLiteral("right_frame_domain"), currentDomain},
                                   {QStringLiteral("frame_coordinates_comparable"), comparableFrames}};

        const QString previousSource = previous.value(QStringLiteral("source_id")).toString();
        const QString currentSource = current.value(QStringLiteral("source_id")).toString();
        const QString previousFingerprint = previous.value(QStringLiteral("source_fingerprint")).toString();
        const QString currentFingerprint = current.value(QStringLiteral("source_fingerprint")).toString();
        const bool sourceChange = previousSource != currentSource;
        const bool fingerprintChange = previousFingerprint != currentFingerprint;
        if (sourceChange || fingerprintChange) {
            QJsonObject warning = edgeBase;
            warning.insert(QStringLiteral("kind"), QStringLiteral("source_provenance_change"));
            warning.insert(QStringLiteral("left_source_id"), previousSource);
            warning.insert(QStringLiteral("right_source_id"), currentSource);
            warning.insert(QStringLiteral("left_source_fingerprint"), previousFingerprint);
            warning.insert(QStringLiteral("right_source_fingerprint"), currentFingerprint);
            warning.insert(QStringLiteral("frame_coordinate_comparison_skipped"), !comparableFrames);
            warning.insert(QStringLiteral("interpretation"), QStringLiteral("review_candidate_not_error"));
            warnings.append(warning);
            ++provenanceChangeCount;
        }

        if (!comparableFrames) {
            ++frameComparisonSkippedCount;
            continue;
        }

        if (currentStart < previousStart) {
            QJsonObject warning = edgeBase;
            warning.insert(QStringLiteral("kind"), QStringLiteral("source_chronology_reversal"));
            warning.insert(QStringLiteral("left_start_frame"), previousStart);
            warning.insert(QStringLiteral("right_start_frame"), currentStart);
            warning.insert(QStringLiteral("interpretation"), QStringLiteral("review_candidate_not_error"));
            warnings.append(warning);
            ++chronologyReversalCount;
        }
        if (previousStart >= 0 && previousEnd > previousStart && currentStart >= 0 && currentEnd > currentStart &&
            currentStart < previousEnd && currentEnd > previousStart) {
            QJsonObject warning = edgeBase;
            warning.insert(QStringLiteral("kind"), QStringLiteral("overlapping_source_ranges"));
            warning.insert(QStringLiteral("left_range_start_frame"), previousStart);
            warning.insert(QStringLiteral("left_range_end_frame"), previousEnd);
            warning.insert(QStringLiteral("right_range_start_frame"), currentStart);
            warning.insert(QStringLiteral("right_range_end_frame"), currentEnd);
            warning.insert(QStringLiteral("interpretation"), QStringLiteral("review_candidate_not_error"));
            warnings.append(warning);
            ++overlapCount;
        }

        if (previousEnd >= 0 && currentStart > previousEnd) {
            sourceGaps.append(QJsonObject{{QStringLiteral("proposal_edge_index"), i - 1},
                                          {QStringLiteral("left_candidate_id"), previousId},
                                          {QStringLiteral("right_candidate_id"), currentId},
                                          {QStringLiteral("frame_domain"), currentDomain},
                                          {QStringLiteral("positive_source_gap_frames"), currentStart - previousEnd},
                                          {QStringLiteral("interpretation"), QStringLiteral("relative_gap_candidate_not_error")}});
        }
    }

    std::sort(sourceGaps.begin(), sourceGaps.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const int aGap = a.value(QStringLiteral("positive_source_gap_frames")).toInt();
        const int bGap = b.value(QStringLiteral("positive_source_gap_frames")).toInt();
        if (aGap != bGap) return aGap > bGap;
        return a.value(QStringLiteral("proposal_edge_index")).toInt() < b.value(QStringLiteral("proposal_edge_index")).toInt();
    });
    while (sourceGaps.size() > 10) sourceGaps.removeLast();
    QJsonArray rankedGaps;
    for (int i = 0; i < sourceGaps.size(); ++i) {
        QJsonObject gap = sourceGaps.at(i);
        gap.insert(QStringLiteral("rank"), i + 1);
        rankedGaps.append(gap);
    }

    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("authority"), QStringLiteral("derived_analysis")},
                       {QStringLiteral("analysis_semantics"), QStringLiteral("structural_continuity_review_candidates_not_quality_judgment")},
                       {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                       {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                       {QStringLiteral("segment_count"), segments.size()},
                       {QStringLiteral("warning_count"), warnings.size()},
                       {QStringLiteral("warnings"), warnings},
                       {QStringLiteral("chronology_reversal_count"), chronologyReversalCount},
                       {QStringLiteral("overlapping_range_count"), overlapCount},
                       {QStringLiteral("repeated_transcript_content_count"), repeatedTextCount},
                       {QStringLiteral("source_provenance_change_count"), provenanceChangeCount},
                       {QStringLiteral("frame_comparison_skipped_due_to_domain_count"), frameComparisonSkippedCount},
                       {QStringLiteral("ranked_positive_source_gap_candidates"), rankedGaps},
                       {QStringLiteral("frame_domain_semantics"), QStringLiteral("frame_order_overlap_gap_only_compared_within_same_provenance_coordinate_domain")},
                       {QStringLiteral("source_gap_threshold_applied"), false},
                       {QStringLiteral("normative_thresholds_applied"), false},
                       {QStringLiteral("quality_claim"), false},
                       {QStringLiteral("executable"), false},
                       {QStringLiteral("mutation_authority"), QStringLiteral("none")}};
}

bool registerVibeCutContinuityTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutToolSurface *surfacePtr = &surface;
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
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
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("rough_cut_continuity_analyze");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Analyze one exact current rough-cut candidate-ID sequence for structural continuity review candidates. Frame chronology/overlap/gap is compared only within the same proven coordinate/provenance domain; cross-domain edges report provenance change without pretending their frame numbers are comparable. No quality threshold or edit authority is applied.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, [surfacePtr](const QJsonObject &args) { return toolHandler(surfacePtr, args); }, error);
}