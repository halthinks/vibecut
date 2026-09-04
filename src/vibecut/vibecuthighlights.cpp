/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuthighlights.h"

#include "vibecutjobmanager.h"
#include "vibecutmediaindex.h"
#include "vibecutroughcutsynthesis.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString hashObject(const QJsonObject &object)
{
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),
                                                         QCryptographicHash::Sha256).toHex());
}

bool supportedFormat(const QString &format)
{
    return format == QLatin1String("highlight_reel") || format == QLatin1String("short") ||
           format == QLatin1String("quote");
}

bool sameCandidateProvenance(const QJsonObject &a, const QJsonObject &b)
{
    const QStringList stringFields{QStringLiteral("candidate_id"), QStringLiteral("kind"),
                                   QStringLiteral("text_sha256"), QStringLiteral("source_id"),
                                   QStringLiteral("source_fingerprint"), QStringLiteral("extractor_id"),
                                   QStringLiteral("extractor_version")};
    for (const QString &field : stringFields) {
        if (a.value(field).toString() != b.value(field).toString()) return false;
    }
    return a.value(QStringLiteral("start_frame")).toInt(-1) == b.value(QStringLiteral("start_frame")).toInt(-2) &&
           a.value(QStringLiteral("end_frame")).toInt(-1) == b.value(QStringLiteral("end_frame")).toInt(-2);
}

bool overlapsAny(int start, int end, const QList<QPair<int, int>> &ranges)
{
    for (const auto &range : ranges) {
        if (start < range.second && end > range.first) return true;
    }
    return false;
}

bool buildCurrentContext(VibeCutToolSurface *surface, int maxCandidates, int maxTextChars,
                         QJsonObject &context, QString *error)
{
    if (error) error->clear();
    if (!surface) {
        if (error) *error = QStringLiteral("Highlight proposal requires the VibeCut tool surface.");
        return false;
    }
    VibeCutMediaIndex index;
    QString indexError;
    if (!index.rebuildFromCurrentProject(&indexError)) {
        if (error) *error = indexError;
        return false;
    }
    context = buildVibeCutRoughCutContext(index.documents(), surface->projectRevision(),
                                          maxCandidates, maxTextChars, error);
    return !context.isEmpty() && (!error || error->isEmpty());
}

QJsonObject buildTool(VibeCutTools *tools, VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!tools || !surface || !tools->jobManager()) return err(QStringLiteral("Highlight proposal requires the VibeCut runtime."));
    const quint64 currentRevision = surface->projectRevision();
    const quint64 baseRevision = static_cast<quint64>(input.value(QStringLiteral("base_revision")).toDouble(-1));
    if (baseRevision != currentRevision) {
        return err(QStringLiteral("Highlight context is stale: base revision %1 does not match current revision %2.")
                       .arg(baseRevision).arg(currentRevision));
    }
    const QString contextSha = input.value(QStringLiteral("context_sha256")).toString().trimmed();
    if (contextSha.size() != 64) return err(QStringLiteral("context_sha256 must contain exactly 64 characters."));
    const int maxCandidates = input.value(QStringLiteral("context_max_candidates")).toInt(200);
    const int maxTextChars = input.value(QStringLiteral("context_max_text_chars")).toInt(600);

    QJsonObject context;
    QString contextError;
    if (!buildCurrentContext(surface, maxCandidates, maxTextChars, context, &contextError)) return err(contextError);
    if (context.value(QStringLiteral("context_sha256")).toString() != contextSha) {
        return err(QStringLiteral("Highlight proposal was not requested against the exact current candidate context."));
    }

    const QString objectiveJobId = input.value(QStringLiteral("objective_job_id")).toString().trimmed();
    VibeCutJob objectiveJob;
    if (objectiveJobId.isEmpty() || !tools->jobManager()->job(objectiveJobId, objectiveJob) ||
        objectiveJob.kind != QLatin1String("rough_cut_objective_rank") || objectiveJob.state != VibeCutJobState::Succeeded) {
        return err(QStringLiteral("objective_job_id must identify a succeeded rough-cut objective-ranking job."));
    }
    if (objectiveJob.result.value(QStringLiteral("base_revision")).toDouble(-1) != static_cast<double>(currentRevision) ||
        objectiveJob.result.value(QStringLiteral("context_sha256")).toString() != contextSha) {
        return err(QStringLiteral("Objective-ranking job is stale or belongs to a different highlight context."));
    }

    const QString format = input.value(QStringLiteral("format")).toString().trimmed().toLower();
    const int maxSegments = input.value(QStringLiteral("max_segments")).toInt(8);
    const qint64 maxTotalFrames = static_cast<qint64>(input.value(QStringLiteral("max_total_frames")).toDouble(-1));
    const double minRelevance = input.value(QStringLiteral("min_relevance")).toDouble(0.0);
    const bool preserveSourceOrder = input.value(QStringLiteral("preserve_source_order")).toBool(true);

    QString buildError;
    QJsonObject result = buildVibeCutHighlightProposal(context, objectiveJob.result, format,
                                                       maxSegments, maxTotalFrames, minRelevance,
                                                       preserveSourceOrder, currentRevision, &buildError);
    if (!buildError.isEmpty()) return err(buildError);
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("objective_job_id"), objectiveJobId);
    return result;
}
} // namespace

QJsonObject buildVibeCutHighlightProposal(const QJsonObject &context,
                                          const QJsonObject &objectiveRanking,
                                          const QString &requestedFormat,
                                          int maxSegments,
                                          qint64 maxTotalFrames,
                                          double minRelevance,
                                          bool preserveSourceOrder,
                                          quint64 currentRevision,
                                          QString *error)
{
    if (error) error->clear();
    const QString format = requestedFormat.trimmed().toLower();
    if (!supportedFormat(format)) {
        if (error) *error = QStringLiteral("Highlight format must be highlight_reel, short, or quote.");
        return {};
    }
    if (maxSegments < 1 || maxSegments > 50) {
        if (error) *error = QStringLiteral("Highlight max_segments must be 1..50.");
        return {};
    }
    if (maxTotalFrames < 1 || maxTotalFrames > std::numeric_limits<int>::max()) {
        if (error) *error = QStringLiteral("Highlight max_total_frames must be a positive supported integer frame budget.");
        return {};
    }
    if (!std::isfinite(minRelevance) || minRelevance < 0.0 || minRelevance > 1.0) {
        if (error) *error = QStringLiteral("Highlight min_relevance must be between 0 and 1.");
        return {};
    }
    if (context.value(QStringLiteral("schema_version")).toInt(-1) != 1 ||
        context.value(QStringLiteral("authority")).toString() != QLatin1String("proposal_context") ||
        context.value(QStringLiteral("base_revision")).toDouble(-1) != static_cast<double>(currentRevision) ||
        context.value(QStringLiteral("context_sha256")).toString().size() != 64 ||
        !context.value(QStringLiteral("candidates")).isArray()) {
        if (error) *error = QStringLiteral("Highlight proposal received a malformed or stale rough-cut context.");
        return {};
    }
    if (objectiveRanking.value(QStringLiteral("kind")).toString() != QLatin1String("rough_cut_objective_rank") ||
        objectiveRanking.value(QStringLiteral("authority")).toString() != QLatin1String("derived_ranking") ||
        objectiveRanking.value(QStringLiteral("score_semantics")).toString() != QLatin1String("current_hybrid_relevance_not_probability") ||
        objectiveRanking.value(QStringLiteral("base_revision")).toDouble(-1) != static_cast<double>(currentRevision) ||
        objectiveRanking.value(QStringLiteral("context_sha256")).toString() != context.value(QStringLiteral("context_sha256")).toString() ||
        !objectiveRanking.value(QStringLiteral("candidates")).isArray()) {
        if (error) *error = QStringLiteral("Highlight proposal requires the exact completed objective-ranking result for this context/revision.");
        return {};
    }
    const QString objective = objectiveRanking.value(QStringLiteral("objective")).toString().trimmed();
    if (objective.isEmpty() || objective.size() > 2048) {
        if (error) *error = QStringLiteral("Highlight objective ranking is missing its bounded objective.");
        return {};
    }

    QHash<QString, QJsonObject> contextById;
    for (const QJsonValue &value : context.value(QStringLiteral("candidates")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject candidate = value.toObject();
        const QString id = candidate.value(QStringLiteral("candidate_id")).toString().trimmed();
        if (!id.isEmpty() && !contextById.contains(id)) contextById.insert(id, candidate);
    }

    QList<QJsonObject> eligible;
    int belowRelevanceSkipped = 0;
    int provenanceMismatchSkipped = 0;
    QSet<QString> seen;
    for (const QJsonValue &value : objectiveRanking.value(QStringLiteral("candidates")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject ranked = value.toObject();
        const QString id = ranked.value(QStringLiteral("candidate_id")).toString().trimmed();
        const double relevance = ranked.value(QStringLiteral("objective_relevance_score")).toDouble(-1.0);
        if (id.isEmpty() || seen.contains(id) || !contextById.contains(id) || !std::isfinite(relevance) || relevance < 0.0 || relevance > 1.0) {
            ++provenanceMismatchSkipped;
            continue;
        }
        seen.insert(id);
        const QJsonObject canonical = contextById.value(id);
        if (!sameCandidateProvenance(canonical, ranked)) {
            ++provenanceMismatchSkipped;
            continue;
        }
        if (relevance < minRelevance) {
            ++belowRelevanceSkipped;
            continue;
        }
        QJsonObject item = canonical;
        item.insert(QStringLiteral("objective_relevance_score"), relevance);
        eligible.append(item);
    }
    std::sort(eligible.begin(), eligible.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const double aScore = a.value(QStringLiteral("objective_relevance_score")).toDouble();
        const double bScore = b.value(QStringLiteral("objective_relevance_score")).toDouble();
        if (aScore != bScore) return aScore > bScore;
        const int aStart = a.value(QStringLiteral("start_frame")).toInt();
        const int bStart = b.value(QStringLiteral("start_frame")).toInt();
        if (aStart != bStart) return aStart < bStart;
        return a.value(QStringLiteral("candidate_id")).toString() < b.value(QStringLiteral("candidate_id")).toString();
    });

    QList<QJsonObject> selected;
    QList<QPair<int, int>> ranges;
    qint64 totalFrames = 0;
    int overlapSkipped = 0;
    int budgetSkipped = 0;
    for (const QJsonObject &candidate : eligible) {
        if (selected.size() >= maxSegments) break;
        const int start = candidate.value(QStringLiteral("start_frame")).toInt(-1);
        const int end = candidate.value(QStringLiteral("end_frame")).toInt(-1);
        if (start < 0 || end <= start) {
            ++provenanceMismatchSkipped;
            continue;
        }
        if (overlapsAny(start, end, ranges)) {
            ++overlapSkipped;
            continue;
        }
        const qint64 duration = static_cast<qint64>(end) - start;
        if (totalFrames + duration > maxTotalFrames) {
            ++budgetSkipped;
            continue;
        }
        selected.append(candidate);
        ranges.append(qMakePair(start, end));
        totalFrames += duration;
    }
    if (selected.isEmpty()) {
        if (error) *error = QStringLiteral("No objective-ranked highlight candidate fits the requested relevance/segment/frame budget.");
        return {};
    }
    if (preserveSourceOrder) {
        std::sort(selected.begin(), selected.end(), [](const QJsonObject &a, const QJsonObject &b) {
            const int aStart = a.value(QStringLiteral("start_frame")).toInt();
            const int bStart = b.value(QStringLiteral("start_frame")).toInt();
            if (aStart != bStart) return aStart < bStart;
            return a.value(QStringLiteral("candidate_id")).toString() < b.value(QStringLiteral("candidate_id")).toString();
        });
    }

    QJsonArray selectedIds;
    QJsonArray segments;
    double relevanceSum = 0.0;
    for (int i = 0; i < selected.size(); ++i) {
        QJsonObject item = selected.at(i);
        const QString id = item.value(QStringLiteral("candidate_id")).toString();
        selectedIds.append(id);
        relevanceSum += item.value(QStringLiteral("objective_relevance_score")).toDouble();
        item.insert(QStringLiteral("proposal_order"), i);
        segments.append(item);
    }
    const double meanRelevance = relevanceSum / selected.size();

    // Re-run the canonical proposal validator so this deterministic selector
    // cannot bypass context-hash/range/id/duration invariants.
    const QJsonObject canonicalProposal{{QStringLiteral("schema_version"), 1},
                                        {QStringLiteral("authority"), QStringLiteral("proposal")},
                                        {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                                        {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                                        {QStringLiteral("objective"), objective},
                                        {QStringLiteral("selected_candidate_ids"), selectedIds},
                                        {QStringLiteral("max_total_frames"), maxTotalFrames}};
    QString validationError;
    const QJsonObject validated = validateVibeCutRoughCutProposal(context, canonicalProposal, currentRevision, &validationError);
    if (!validationError.isEmpty()) {
        if (error) *error = QStringLiteral("Highlight selection failed canonical proposal validation: %1").arg(validationError);
        return {};
    }

    QJsonObject identity{{QStringLiteral("schema_version"), 1},
                         {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                         {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                         {QStringLiteral("objective"), objective},
                         {QStringLiteral("format"), format},
                         {QStringLiteral("max_segments"), maxSegments},
                         {QStringLiteral("max_total_frames"), maxTotalFrames},
                         {QStringLiteral("min_relevance"), minRelevance},
                         {QStringLiteral("preserve_source_order"), preserveSourceOrder},
                         {QStringLiteral("selected_candidate_ids"), selectedIds}};

    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("authority"), QStringLiteral("proposal")},
                       {QStringLiteral("proposal_id"), hashObject(identity)},
                       {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                       {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                       {QStringLiteral("objective"), objective},
                       {QStringLiteral("format"), format},
                       {QStringLiteral("selection_rubric"), QJsonObject{
                           {QStringLiteral("primary_order"), QStringLiteral("descending_current_objective_relevance")},
                           {QStringLiteral("min_relevance"), minRelevance},
                           {QStringLiteral("max_segments"), maxSegments},
                           {QStringLiteral("max_total_frames"), maxTotalFrames},
                           {QStringLiteral("reject_overlaps"), true},
                           {QStringLiteral("preserve_source_order_in_output"), preserveSourceOrder}}},
                       {QStringLiteral("score_semantics"), QStringLiteral("objective_relevance_ranked_budgeted_selection_not_quality_probability")},
                       {QStringLiteral("segment_count"), segments.size()},
                       {QStringLiteral("total_frames"), totalFrames},
                       {QStringLiteral("mean_objective_relevance"), meanRelevance},
                       {QStringLiteral("below_relevance_skipped"), belowRelevanceSkipped},
                       {QStringLiteral("provenance_mismatch_skipped"), provenanceMismatchSkipped},
                       {QStringLiteral("overlap_skipped"), overlapSkipped},
                       {QStringLiteral("budget_skipped"), budgetSkipped},
                       {QStringLiteral("selected_candidate_ids"), selectedIds},
                       {QStringLiteral("segments"), segments},
                       {QStringLiteral("canonical_rough_cut_proposal_id"), validated.value(QStringLiteral("proposal_id"))},
                       {QStringLiteral("executable"), false},
                       {QStringLiteral("mutation_authority"), QStringLiteral("none")},
                       {QStringLiteral("next_step"), QStringLiteral("review_and_evaluate_highlight_proposal")}};
}

bool registerVibeCutHighlightTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("Highlight proposal requires native VibeCutTools/JobManager.");
        return false;
    }
    VibeCutToolSurface *surfacePtr = &surface;
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("base_revision"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                {QStringLiteral("context_sha256"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 64}, {QStringLiteral("maxLength"), 64}}},
                                {QStringLiteral("context_max_candidates"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 300}}},
                                {QStringLiteral("context_max_text_chars"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 64}, {QStringLiteral("maximum"), 2048}}},
                                {QStringLiteral("objective_job_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("format"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                       {QStringLiteral("enum"), QJsonArray{QStringLiteral("highlight_reel"), QStringLiteral("short"), QStringLiteral("quote")}}}},
                                {QStringLiteral("max_segments"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 50}}},
                                {QStringLiteral("max_total_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}},
                                {QStringLiteral("min_relevance"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                {QStringLiteral("preserve_source_order"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("base_revision"), QStringLiteral("context_sha256"),
                                                                    QStringLiteral("context_max_candidates"), QStringLiteral("context_max_text_chars"),
                                                                    QStringLiteral("objective_job_id"), QStringLiteral("format"),
                                                                    QStringLiteral("max_segments"), QStringLiteral("max_total_frames")}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("highlight_proposal_build");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Build a proposal-only highlight/short/quote sequence from one exact current rough-cut context and completed objective-ranking job. Selection is deterministic by disclosed objective-relevance, overlap and frame-budget rules; ranges/provenance are canonical and no edit is executed.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, [tools, surfacePtr](const QJsonObject &args) { return buildTool(tools, surfacePtr, args); }, error);
}
