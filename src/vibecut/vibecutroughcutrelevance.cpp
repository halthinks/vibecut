/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutroughcutrelevance.h"

#include "vibecutjobmanager.h"
#include "vibecutmediaindex.h"
#include "vibecutroughcutsynthesis.h"
#include "vibecuttools.h"
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

QString stateName(VibeCutJobState state)
{
    switch (state) {
    case VibeCutJobState::Queued: return QStringLiteral("queued");
    case VibeCutJobState::Running: return QStringLiteral("running");
    case VibeCutJobState::CancelRequested: return QStringLiteral("cancel_requested");
    case VibeCutJobState::Succeeded: return QStringLiteral("succeeded");
    case VibeCutJobState::Failed: return QStringLiteral("failed");
    case VibeCutJobState::Cancelled: return QStringLiteral("cancelled");
    }
    return QStringLiteral("unknown");
}

bool buildCurrentContext(VibeCutToolSurface *surface,
                         int maxCandidates,
                         int maxTextChars,
                         QJsonObject &context,
                         QString *error)
{
    if (error) error->clear();
    if (!surface) {
        if (error) *error = QStringLiteral("Rough-cut relevance requires the VibeCut tool surface.");
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
    return error == nullptr || error->isEmpty();
}

void finalizeObjectiveChild(VibeCutJobManager *jobs,
                            VibeCutToolSurface *surface,
                            const QString &parentId,
                            const QString &childId,
                            quint64 baseRevision,
                            const QString &contextSha,
                            int contextMaxCandidates,
                            int contextMaxTextChars,
                            const QString &objective,
                            int limit,
                            double minScore)
{
    if (!jobs || !surface) return;
    VibeCutJob parent;
    if (!jobs->job(parentId, parent) || parent.terminal()) return;
    if (parent.state == VibeCutJobState::CancelRequested) {
        jobs->requestCancel(childId);
        jobs->markCancelled(parentId, QStringLiteral("Rough-cut objective ranking cancelled."));
        return;
    }

    VibeCutJob child;
    if (!jobs->job(childId, child) || !child.terminal()) return;
    if (surface->projectRevision() != baseRevision) {
        jobs->markFailed(parentId, QStringLiteral("Project revision changed while rough-cut relevance was running; refusing stale ranking."));
        return;
    }

    QJsonObject currentContext;
    QString contextError;
    if (!buildCurrentContext(surface, contextMaxCandidates, contextMaxTextChars, currentContext, &contextError)) {
        jobs->markFailed(parentId, QStringLiteral("Could not rebuild current rough-cut context: %1").arg(contextError));
        return;
    }
    if (currentContext.value(QStringLiteral("context_sha256")).toString() != contextSha) {
        jobs->markFailed(parentId, QStringLiteral("Transcript/evidence candidate context changed while rough-cut relevance was running; refusing stale ranking."));
        return;
    }
    if (child.state != VibeCutJobState::Succeeded) {
        jobs->markFailed(parentId, QStringLiteral("Hybrid retrieval child failed: %1").arg(child.message));
        return;
    }

    QString rankError;
    QJsonObject ranked = rankVibeCutRoughCutObjectiveHits(currentContext, child.result, limit, minScore, &rankError);
    if (!rankError.isEmpty()) {
        jobs->markFailed(parentId, rankError);
        return;
    }
    ranked.insert(QStringLiteral("kind"), QStringLiteral("rough_cut_objective_rank"));
    ranked.insert(QStringLiteral("objective"), objective);
    ranked.insert(QStringLiteral("hybrid_child_job_id"), childId);
    QString resultError;
    if (!jobs->setResult(parentId, ranked, &resultError)) {
        jobs->markFailed(parentId, QStringLiteral("Rough-cut relevance completed but its structured result was rejected: %1").arg(resultError));
        return;
    }
    jobs->markSucceeded(parentId, QStringLiteral("Ranked %1 current rough-cut candidate(s) for the objective.")
                                   .arg(ranked.value(QStringLiteral("candidate_count")).toInt()));
}

QJsonObject startObjectiveRank(VibeCutTools *tools, VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!tools || !surface || !tools->jobManager()) return err(QStringLiteral("Rough-cut objective ranking requires the VibeCut runtime."));
    const quint64 currentRevision = surface->projectRevision();
    const quint64 baseRevision = static_cast<quint64>(input.value(QStringLiteral("base_revision")).toDouble(-1));
    if (baseRevision != currentRevision) {
        return err(QStringLiteral("Rough-cut objective context is stale: base revision %1 does not match current revision %2.")
                       .arg(baseRevision).arg(currentRevision));
    }
    const QString contextSha = input.value(QStringLiteral("context_sha256")).toString().trimmed();
    if (contextSha.size() != 64) return err(QStringLiteral("context_sha256 must contain exactly 64 hexadecimal characters."));
    const int maxCandidates = input.value(QStringLiteral("context_max_candidates")).toInt(200);
    const int maxTextChars = input.value(QStringLiteral("context_max_text_chars")).toInt(600);
    const QString objective = input.value(QStringLiteral("objective")).toString().trimmed();
    if (objective.isEmpty() || objective.size() > 2048) return err(QStringLiteral("objective must contain 1..2048 characters."));
    const int limit = qBound(1, input.value(QStringLiteral("limit")).toInt(50), 100);
    const double minScore = qBound(0.0, input.value(QStringLiteral("min_score")).toDouble(0.0), 1.0);
    const QString device = input.value(QStringLiteral("device")).toString(QStringLiteral("auto")).trimmed().toLower();
    if (device != QLatin1String("auto") && device != QLatin1String("cpu") && device != QLatin1String("cuda")) {
        return err(QStringLiteral("device must be auto, cpu, or cuda."));
    }

    QJsonObject context;
    QString contextError;
    if (!buildCurrentContext(surface, maxCandidates, maxTextChars, context, &contextError)) return err(contextError);
    if (context.value(QStringLiteral("context_sha256")).toString() != contextSha) {
        return err(QStringLiteral("Rough-cut objective ranking was not requested against the exact current candidate context."));
    }

    const QJsonObject childStart = surface->invoke(QStringLiteral("media_search_hybrid"),
                                                   QJsonObject{{QStringLiteral("query"), objective},
                                                               {QStringLiteral("limit"), 100},
                                                               {QStringLiteral("min_score"), 0.0},
                                                               {QStringLiteral("device"), device}});
    if (!childStart.value(QStringLiteral("ok")).toBool(false)) {
        return err(QStringLiteral("Rough-cut objective ranking requires current hybrid retrieval: %1")
                       .arg(childStart.value(QStringLiteral("error")).toString()));
    }
    const QString childId = childStart.value(QStringLiteral("job_id")).toString().trimmed();
    if (childId.isEmpty()) return err(QStringLiteral("Hybrid retrieval did not return a job id."));

    VibeCutJobManager *jobs = tools->jobManager();
    const QString parentId = jobs->createJob(QStringLiteral("rough_cut_objective_rank"),
                                             QStringLiteral("Rough-cut relevance · %1").arg(objective.left(80)), true);
    jobs->markRunning(parentId, QStringLiteral("Waiting for current hybrid retrieval and filtering to the exact rough-cut context…"));

    QObject::connect(jobs, &VibeCutJobManager::jobChanged, jobs,
                     [jobs, surface, parentId, childId, baseRevision, contextSha, maxCandidates, maxTextChars,
                      objective, limit, minScore](const QString &changedId) {
        if (changedId != childId) return;
        finalizeObjectiveChild(jobs, surface, parentId, childId, baseRevision, contextSha,
                               maxCandidates, maxTextChars, objective, limit, minScore);
    });
    QObject::connect(jobs, &VibeCutJobManager::jobChanged, jobs,
                     [jobs, parentId, childId](const QString &changedId) {
        if (changedId != parentId) return;
        VibeCutJob parent;
        if (!jobs->job(parentId, parent) || parent.state != VibeCutJobState::CancelRequested) return;
        jobs->requestCancel(childId);
    });

    finalizeObjectiveChild(jobs, surface, parentId, childId, baseRevision, contextSha,
                           maxCandidates, maxTextChars, objective, limit, minScore);

    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("started"), true},
                       {QStringLiteral("job_id"), parentId},
                       {QStringLiteral("hybrid_child_job_id"), childId},
                       {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)},
                       {QStringLiteral("context_sha256"), contextSha}};
}

QJsonObject resultTool(VibeCutTools *tools, const QJsonObject &input)
{
    if (!tools || !tools->jobManager()) return err(QStringLiteral("VibeCut JobManager is unavailable."));
    const QString id = input.value(QStringLiteral("job_id")).toString().trimmed();
    if (id.isEmpty()) return err(QStringLiteral("job_id must not be empty."));
    VibeCutJob job;
    if (!tools->jobManager()->job(id, job) || job.kind != QLatin1String("rough_cut_objective_rank")) {
        return err(QStringLiteral("Unknown rough-cut objective-ranking job: %1").arg(id));
    }
    if (!job.terminal()) return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("ready"), false},
                                             {QStringLiteral("job_id"), id}, {QStringLiteral("state"), stateName(job.state)},
                                             {QStringLiteral("progress"), job.progress}, {QStringLiteral("message"), job.message}};
    if (job.state != VibeCutJobState::Succeeded) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("ready"), true},
                                                                     {QStringLiteral("job_id"), id}, {QStringLiteral("state"), stateName(job.state)},
                                                                     {QStringLiteral("error"), job.message}};
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("ready"), true},
                       {QStringLiteral("job_id"), id}, {QStringLiteral("state"), stateName(job.state)},
                       {QStringLiteral("message"), job.message}, {QStringLiteral("result"), job.result}};
}
} // namespace

QJsonObject rankVibeCutRoughCutObjectiveHits(const QJsonObject &context,
                                             const QJsonObject &hybridResult,
                                             int limit,
                                             double minScore,
                                             QString *error)
{
    if (error) error->clear();
    if (context.value(QStringLiteral("schema_version")).toInt(-1) != 1 ||
        context.value(QStringLiteral("authority")).toString() != QLatin1String("proposal_context") ||
        context.value(QStringLiteral("context_sha256")).toString().size() != 64 ||
        !context.value(QStringLiteral("candidates")).isArray()) {
        if (error) *error = QStringLiteral("Rough-cut objective ranking received a malformed candidate context.");
        return {};
    }
    if (hybridResult.value(QStringLiteral("kind")).toString() != QLatin1String("semantic_hybrid_search") ||
        hybridResult.value(QStringLiteral("authority")).toString() != QLatin1String("derived_ranking") ||
        hybridResult.value(QStringLiteral("score_semantics")).toString() != QLatin1String("weighted_current_lexical_and_minilm_similarity_not_probability") ||
        !hybridResult.value(QStringLiteral("hits")).isArray()) {
        if (error) *error = QStringLiteral("Rough-cut objective ranking requires a canonical current hybrid-search result.");
        return {};
    }
    const qint64 contextRevision = static_cast<qint64>(context.value(QStringLiteral("base_revision")).toDouble(-1));
    const qint64 resultRevision = static_cast<qint64>(hybridResult.value(QStringLiteral("base_revision")).toDouble(-2));
    if (contextRevision < 0 || resultRevision != contextRevision) {
        if (error) *error = QStringLiteral("Hybrid-search result revision does not match the rough-cut context revision.");
        return {};
    }
    limit = qBound(1, limit, 100);
    minScore = qBound(0.0, minScore, 1.0);

    QHash<QString, QJsonObject> candidates;
    for (const QJsonValue &value : context.value(QStringLiteral("candidates")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject candidate = value.toObject();
        const QString id = candidate.value(QStringLiteral("candidate_id")).toString();
        if (!id.isEmpty()) candidates.insert(id, candidate);
    }

    QList<QJsonObject> ranked;
    int nonCandidateSkipped = 0;
    int provenanceMismatchSkipped = 0;
    int belowScoreSkipped = 0;
    for (const QJsonValue &value : hybridResult.value(QStringLiteral("hits")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject hit = value.toObject();
        const QString id = hit.value(QStringLiteral("anchor_id")).toString();
        if (!candidates.contains(id)) {
            ++nonCandidateSkipped;
            continue;
        }
        const double score = hit.value(QStringLiteral("hybrid_score")).toDouble(-1.0);
        if (!std::isfinite(score) || score < 0.0 || score > 1.0) {
            ++provenanceMismatchSkipped;
            continue;
        }
        if (score < minScore) {
            ++belowScoreSkipped;
            continue;
        }
        QJsonObject candidate = candidates.value(id);
        if (hit.value(QStringLiteral("kind")).toString() != candidate.value(QStringLiteral("kind")).toString() ||
            hit.value(QStringLiteral("start_frame")).toInt(-1) != candidate.value(QStringLiteral("start_frame")).toInt(-2) ||
            hit.value(QStringLiteral("end_frame")).toInt(-1) != candidate.value(QStringLiteral("end_frame")).toInt(-2)) {
            ++provenanceMismatchSkipped;
            continue;
        }
        const QString candidateSource = candidate.value(QStringLiteral("source_id")).toString();
        const QString candidateFingerprint = candidate.value(QStringLiteral("source_fingerprint")).toString();
        if (!candidateSource.isEmpty()) {
            const QJsonObject metadata = hit.value(QStringLiteral("metadata")).toObject();
            if (metadata.value(QStringLiteral("source_id")).toString() != candidateSource ||
                metadata.value(QStringLiteral("source_fingerprint")).toString() != candidateFingerprint) {
                ++provenanceMismatchSkipped;
                continue;
            }
        }
        candidate.insert(QStringLiteral("objective_relevance_score"), score);
        candidate.insert(QStringLiteral("score_semantics"), QStringLiteral("current_hybrid_relevance_not_probability"));
        candidate.insert(QStringLiteral("lexical_available"), hit.value(QStringLiteral("lexical_available")));
        candidate.insert(QStringLiteral("lexical_component"), hit.value(QStringLiteral("lexical_component")));
        candidate.insert(QStringLiteral("semantic_available"), hit.value(QStringLiteral("semantic_available")));
        candidate.insert(QStringLiteral("semantic_component"), hit.value(QStringLiteral("semantic_component")));
        ranked.append(candidate);
    }
    std::sort(ranked.begin(), ranked.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const double aScore = a.value(QStringLiteral("objective_relevance_score")).toDouble();
        const double bScore = b.value(QStringLiteral("objective_relevance_score")).toDouble();
        if (aScore != bScore) return aScore > bScore;
        const int aStart = a.value(QStringLiteral("start_frame")).toInt();
        const int bStart = b.value(QStringLiteral("start_frame")).toInt();
        if (aStart != bStart) return aStart < bStart;
        return a.value(QStringLiteral("candidate_id")).toString() < b.value(QStringLiteral("candidate_id")).toString();
    });
    while (ranked.size() > limit) ranked.removeLast();
    QJsonArray output;
    for (const QJsonObject &candidate : ranked) output.append(candidate);

    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("authority"), QStringLiteral("derived_ranking")},
                       {QStringLiteral("score_semantics"), QStringLiteral("current_hybrid_relevance_not_probability")},
                       {QStringLiteral("base_revision"), context.value(QStringLiteral("base_revision"))},
                       {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                       {QStringLiteral("candidate_count"), output.size()},
                       {QStringLiteral("non_candidate_hits_skipped"), nonCandidateSkipped},
                       {QStringLiteral("provenance_mismatch_hits_skipped"), provenanceMismatchSkipped},
                       {QStringLiteral("below_score_hits_skipped"), belowScoreSkipped},
                       {QStringLiteral("candidates"), output},
                       {QStringLiteral("executable"), false},
                       {QStringLiteral("mutation_authority"), QStringLiteral("none")}};
}

bool registerVibeCutRoughCutRelevanceTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("Rough-cut relevance requires native VibeCutTools/JobManager.");
        return false;
    }
    VibeCutToolSurface *surfacePtr = &surface;
    const QJsonObject rankInput{{QStringLiteral("type"), QStringLiteral("object")},
                                {QStringLiteral("properties"), QJsonObject{
                                    {QStringLiteral("base_revision"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                    {QStringLiteral("context_sha256"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 64}, {QStringLiteral("maxLength"), 64}}},
                                    {QStringLiteral("context_max_candidates"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 300}}},
                                    {QStringLiteral("context_max_text_chars"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 64}, {QStringLiteral("maximum"), 2048}}},
                                    {QStringLiteral("objective"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 2048}}},
                                    {QStringLiteral("limit"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 100}}},
                                    {QStringLiteral("min_score"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                    {QStringLiteral("device"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                            {QStringLiteral("enum"), QJsonArray{QStringLiteral("auto"), QStringLiteral("cpu"), QStringLiteral("cuda")}}}}}},
                                {QStringLiteral("required"), QJsonArray{QStringLiteral("base_revision"), QStringLiteral("context_sha256"),
                                                                        QStringLiteral("context_max_candidates"), QStringLiteral("context_max_text_chars"),
                                                                        QStringLiteral("objective")}},
                                {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy rankPolicy;
    rankPolicy.name = QStringLiteral("rough_cut_objective_rank");
    rankPolicy.risk = VibeCutToolRisk::ReadOnly;
    rankPolicy.asynchronous = true;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), rankPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Rank candidates from one exact revision-bound rough-cut context against an editorial objective using the current-only hybrid lexical+MiniLM retrieval path. Context is rebuilt and re-hashed when the child completes; changed transcript/evidence is refused. Output is derived ranking only.")},
                                          {QStringLiteral("input_schema"), rankInput}},
                              rankPolicy, [tools, surfacePtr](const QJsonObject &input) { return startObjectiveRank(tools, surfacePtr, input); }, error)) return false;

    const QJsonObject resultInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{{QStringLiteral("job_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("job_id")}},
                                  {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy resultPolicy;
    resultPolicy.name = QStringLiteral("rough_cut_objective_result");
    resultPolicy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), resultPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Read the state/result of one revision-bound rough-cut objective-ranking job.")},
                                            {QStringLiteral("input_schema"), resultInput}},
                                resultPolicy, [tools](const QJsonObject &input) { return resultTool(tools, input); }, error);
}
