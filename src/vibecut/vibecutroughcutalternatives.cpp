/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutroughcutalternatives.h"

#include "vibecutjobmanager.h"
#include "vibecutmediaindex.h"
#include "vibecutroughcutsynthesis.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

bool buildCurrentContext(VibeCutToolSurface *surface, int maxCandidates, int maxTextChars,
                         QJsonObject &context, QString *error)
{
    if (error) error->clear();
    if (!surface) {
        if (error) *error = QStringLiteral("Rough-cut alternative comparison requires the VibeCut tool surface.");
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

QJsonObject compareTool(VibeCutTools *tools, VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!tools || !surface || !tools->jobManager()) return err(QStringLiteral("Rough-cut alternative comparison requires the VibeCut runtime."));
    const quint64 currentRevision = surface->projectRevision();
    const quint64 baseRevision = static_cast<quint64>(input.value(QStringLiteral("base_revision")).toDouble(-1));
    if (baseRevision != currentRevision) {
        return err(QStringLiteral("Rough-cut alternative context is stale: base revision %1 does not match current revision %2.")
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
        return err(QStringLiteral("Rough-cut alternative comparison was not requested against the exact current candidate context."));
    }

    const QString objectiveJobId = input.value(QStringLiteral("objective_job_id")).toString().trimmed();
    if (objectiveJobId.isEmpty()) return err(QStringLiteral("objective_job_id must not be empty."));
    VibeCutJob objectiveJob;
    if (!tools->jobManager()->job(objectiveJobId, objectiveJob) ||
        objectiveJob.kind != QLatin1String("rough_cut_objective_rank")) {
        return err(QStringLiteral("objective_job_id does not identify a rough-cut objective-ranking job."));
    }
    if (objectiveJob.state != VibeCutJobState::Succeeded) {
        return err(QStringLiteral("Rough-cut objective-ranking job must have succeeded before alternatives can be compared."));
    }
    const QJsonObject ranking = objectiveJob.result;
    if (ranking.value(QStringLiteral("base_revision")).toDouble(-1) != static_cast<double>(currentRevision) ||
        ranking.value(QStringLiteral("context_sha256")).toString() != contextSha) {
        return err(QStringLiteral("Objective-ranking result is stale or belongs to a different rough-cut context."));
    }

    QString compareError;
    QJsonObject result = compareVibeCutRoughCutAlternatives(context, ranking,
                                                            input.value(QStringLiteral("alternatives")).toArray(),
                                                            currentRevision, &compareError);
    if (!compareError.isEmpty()) return err(compareError);
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("objective_job_id"), objectiveJobId);
    return result;
}
} // namespace

QJsonObject compareVibeCutRoughCutAlternatives(const QJsonObject &context,
                                               const QJsonObject &objectiveRanking,
                                               const QJsonArray &alternatives,
                                               quint64 currentRevision,
                                               QString *error)
{
    if (error) error->clear();
    if (context.value(QStringLiteral("schema_version")).toInt(-1) != 1 ||
        context.value(QStringLiteral("authority")).toString() != QLatin1String("proposal_context") ||
        context.value(QStringLiteral("context_sha256")).toString().size() != 64) {
        if (error) *error = QStringLiteral("Alternative comparison received a malformed rough-cut context.");
        return {};
    }
    const qint64 contextRevision = static_cast<qint64>(context.value(QStringLiteral("base_revision")).toDouble(-1));
    if (contextRevision < 0 || static_cast<quint64>(contextRevision) != currentRevision) {
        if (error) *error = QStringLiteral("Alternative comparison context is stale.");
        return {};
    }
    if (objectiveRanking.value(QStringLiteral("kind")).toString() != QLatin1String("rough_cut_objective_rank") ||
        objectiveRanking.value(QStringLiteral("authority")).toString() != QLatin1String("derived_ranking") ||
        objectiveRanking.value(QStringLiteral("score_semantics")).toString() != QLatin1String("current_hybrid_relevance_not_probability") ||
        objectiveRanking.value(QStringLiteral("context_sha256")).toString() != context.value(QStringLiteral("context_sha256")).toString() ||
        objectiveRanking.value(QStringLiteral("base_revision")).toDouble(-1) != static_cast<double>(currentRevision) ||
        !objectiveRanking.value(QStringLiteral("candidates")).isArray()) {
        if (error) *error = QStringLiteral("Alternative comparison requires the exact completed objective-ranking result for this context/revision.");
        return {};
    }
    const QString objective = objectiveRanking.value(QStringLiteral("objective")).toString().trimmed();
    if (objective.isEmpty() || objective.size() > 2048) {
        if (error) *error = QStringLiteral("Objective-ranking result is missing its bounded editorial objective.");
        return {};
    }
    if (alternatives.size() < 2 || alternatives.size() > 5) {
        if (error) *error = QStringLiteral("Alternative comparison requires 2..5 alternatives.");
        return {};
    }

    QHash<QString, double> relevance;
    for (const QJsonValue &value : objectiveRanking.value(QStringLiteral("candidates")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject candidate = value.toObject();
        const QString id = candidate.value(QStringLiteral("candidate_id")).toString().trimmed();
        const double score = candidate.value(QStringLiteral("objective_relevance_score")).toDouble(-1.0);
        if (id.isEmpty() || !std::isfinite(score) || score < 0.0 || score > 1.0 || relevance.contains(id)) continue;
        relevance.insert(id, score);
    }

    QSet<QString> alternativeIds;
    QList<QJsonObject> scored;
    for (const QJsonValue &value : alternatives) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("Each rough-cut alternative must be an object.");
            return {};
        }
        const QJsonObject alternative = value.toObject();
        const QString alternativeId = alternative.value(QStringLiteral("alternative_id")).toString().trimmed();
        if (alternativeId.isEmpty() || alternativeId.size() > 128 || alternativeIds.contains(alternativeId)) {
            if (error) *error = QStringLiteral("Alternative IDs must be unique non-empty strings up to 128 characters.");
            return {};
        }
        alternativeIds.insert(alternativeId);
        QJsonObject proposal{{QStringLiteral("schema_version"), 1},
                             {QStringLiteral("authority"), QStringLiteral("proposal")},
                             {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                             {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                             {QStringLiteral("objective"), objective},
                             {QStringLiteral("selected_candidate_ids"), alternative.value(QStringLiteral("selected_candidate_ids"))}};
        if (alternative.contains(QStringLiteral("max_total_frames"))) {
            proposal.insert(QStringLiteral("max_total_frames"), alternative.value(QStringLiteral("max_total_frames")));
        }
        QString proposalError;
        const QJsonObject validated = validateVibeCutRoughCutProposal(context, proposal, currentRevision, &proposalError);
        if (!proposalError.isEmpty()) {
            if (error) *error = QStringLiteral("Alternative '%1' is invalid: %2").arg(alternativeId, proposalError);
            return {};
        }

        const QJsonArray segments = validated.value(QStringLiteral("segments")).toArray();
        int relevanceCount = 0;
        double relevanceSum = 0.0;
        int provenanceCount = 0;
        for (const QJsonValue &segmentValue : segments) {
            const QJsonObject segment = segmentValue.toObject();
            const QString id = segment.value(QStringLiteral("candidate_id")).toString();
            if (relevance.contains(id)) {
                relevanceSum += relevance.value(id);
                ++relevanceCount;
            }
            if (!segment.value(QStringLiteral("evidence_origin")).toString().isEmpty() ||
                !segment.value(QStringLiteral("source_fingerprint")).toString().isEmpty()) ++provenanceCount;
        }
        const int segmentCount = qMax(1, segments.size());
        const double meanRelevance = relevanceCount > 0 ? relevanceSum / relevanceCount : 0.0;
        const double retrievalCoverage = static_cast<double>(relevanceCount) / segmentCount;
        const double chronology = validated.value(QStringLiteral("reorders_timeline")).toBool(false) ? 0.5 : 1.0;
        const int overlapWarnings = validated.value(QStringLiteral("overlap_warning_count")).toInt(0);
        const double overlapCleanliness = 1.0 / (1.0 + qMax(0, overlapWarnings));
        const double provenanceCoverage = static_cast<double>(provenanceCount) / segmentCount;
        const double comparisonScore = qBound(0.0,
            0.60 * meanRelevance + 0.15 * retrievalCoverage + 0.10 * chronology +
            0.10 * overlapCleanliness + 0.05 * provenanceCoverage,
            1.0);

        scored.append(QJsonObject{{QStringLiteral("alternative_id"), alternativeId},
                                  {QStringLiteral("proposal_id"), validated.value(QStringLiteral("proposal_id"))},
                                  {QStringLiteral("selected_candidate_ids"), alternative.value(QStringLiteral("selected_candidate_ids"))},
                                  {QStringLiteral("segment_count"), validated.value(QStringLiteral("segment_count"))},
                                  {QStringLiteral("total_frames"), validated.value(QStringLiteral("total_frames"))},
                                  {QStringLiteral("mean_objective_relevance_available"), meanRelevance},
                                  {QStringLiteral("objective_relevance_coverage"), retrievalCoverage},
                                  {QStringLiteral("chronology_component"), chronology},
                                  {QStringLiteral("overlap_cleanliness_component"), overlapCleanliness},
                                  {QStringLiteral("provenance_coverage"), provenanceCoverage},
                                  {QStringLiteral("reorders_timeline"), validated.value(QStringLiteral("reorders_timeline"))},
                                  {QStringLiteral("overlap_warning_count"), overlapWarnings},
                                  {QStringLiteral("comparison_score"), comparisonScore},
                                  {QStringLiteral("score_semantics"), QStringLiteral("fixed_transparent_editorial_comparison_not_probability")}});
    }

    std::sort(scored.begin(), scored.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const double aScore = a.value(QStringLiteral("comparison_score")).toDouble();
        const double bScore = b.value(QStringLiteral("comparison_score")).toDouble();
        if (aScore != bScore) return aScore > bScore;
        const double aRel = a.value(QStringLiteral("mean_objective_relevance_available")).toDouble();
        const double bRel = b.value(QStringLiteral("mean_objective_relevance_available")).toDouble();
        if (aRel != bRel) return aRel > bRel;
        const int aFrames = a.value(QStringLiteral("total_frames")).toInt();
        const int bFrames = b.value(QStringLiteral("total_frames")).toInt();
        if (aFrames != bFrames) return aFrames < bFrames;
        return a.value(QStringLiteral("alternative_id")).toString() < b.value(QStringLiteral("alternative_id")).toString();
    });

    QJsonArray output;
    for (int i = 0; i < scored.size(); ++i) {
        QJsonObject item = scored.at(i);
        item.insert(QStringLiteral("rank"), i + 1);
        output.append(item);
    }
    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("authority"), QStringLiteral("derived_comparison")},
                       {QStringLiteral("score_semantics"), QStringLiteral("fixed_transparent_editorial_comparison_not_probability")},
                       {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                       {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                       {QStringLiteral("objective"), objective},
                       {QStringLiteral("weights"), QJsonObject{{QStringLiteral("objective_relevance"), 0.60},
                                                               {QStringLiteral("retrieval_coverage"), 0.15},
                                                               {QStringLiteral("chronology"), 0.10},
                                                               {QStringLiteral("overlap_cleanliness"), 0.10},
                                                               {QStringLiteral("provenance_coverage"), 0.05}}},
                       {QStringLiteral("alternative_count"), output.size()},
                       {QStringLiteral("top_ranked_alternative_id"), output.at(0).toObject().value(QStringLiteral("alternative_id"))},
                       {QStringLiteral("alternatives"), output},
                       {QStringLiteral("executable"), false},
                       {QStringLiteral("mutation_authority"), QStringLiteral("none")},
                       {QStringLiteral("note"), QStringLiteral("Top-ranked means highest under the declared fixed rubric only; it is not an automatic editorial decision or quality probability.")}};
}

bool registerVibeCutRoughCutAlternativeTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("Rough-cut alternative comparison requires native VibeCutTools/JobManager.");
        return false;
    }
    VibeCutToolSurface *surfacePtr = &surface;
    const QJsonObject alternativeSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                        {QStringLiteral("properties"), QJsonObject{
                                            {QStringLiteral("alternative_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 128}}},
                                            {QStringLiteral("selected_candidate_ids"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                                                    {QStringLiteral("minItems"), 1}, {QStringLiteral("maxItems"), 100},
                                                                                                    {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                            {QStringLiteral("max_total_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}}}},
                                        {QStringLiteral("required"), QJsonArray{QStringLiteral("alternative_id"), QStringLiteral("selected_candidate_ids")}},
                                        {QStringLiteral("additionalProperties"), false}};
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("base_revision"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                {QStringLiteral("context_sha256"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 64}, {QStringLiteral("maxLength"), 64}}},
                                {QStringLiteral("context_max_candidates"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 300}}},
                                {QStringLiteral("context_max_text_chars"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 64}, {QStringLiteral("maximum"), 2048}}},
                                {QStringLiteral("objective_job_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("alternatives"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                              {QStringLiteral("minItems"), 2}, {QStringLiteral("maxItems"), 5},
                                                                              {QStringLiteral("items"), alternativeSchema}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("base_revision"), QStringLiteral("context_sha256"),
                                                                    QStringLiteral("context_max_candidates"), QStringLiteral("context_max_text_chars"),
                                                                    QStringLiteral("objective_job_id"), QStringLiteral("alternatives")}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("rough_cut_alternatives_compare");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Compare 2..5 already candidate-bound rough-cut alternatives against one completed current objective-ranking job using a fixed transparent rubric. Returns derived comparison only; it does not choose or execute an edit.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, [tools, surfacePtr](const QJsonObject &args) { return compareTool(tools, surfacePtr, args); }, error);
}
