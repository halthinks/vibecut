/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutbroll.h"

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

bool supportedPurpose(const QString &purpose)
{
    return purpose == QLatin1String("illustrate_subject") || purpose == QLatin1String("show_process") ||
           purpose == QLatin1String("establish_context") || purpose == QLatin1String("cover_edit") ||
           purpose == QLatin1String("show_detail") || purpose == QLatin1String("show_result");
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
        if (error) *error = QStringLiteral("B-roll requires the VibeCut tool surface.");
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

QJsonObject validateOpportunityTool(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface) return err(QStringLiteral("B-roll opportunity validation requires the VibeCut tool surface."));
    QString numberError;
    qint64 baseRevision = -1, maxCandidates = 200, maxTextChars = 600;
    constexpr qint64 MaxExactJsonInteger = 9007199254740991LL;
    if (!exactInteger(input, QStringLiteral("base_revision"), 0, MaxExactJsonInteger, -1, true, baseRevision, &numberError) ||
        !exactInteger(input, QStringLiteral("context_max_candidates"), 1, 300, 200, true, maxCandidates, &numberError) ||
        !exactInteger(input, QStringLiteral("context_max_text_chars"), 64, 2048, 600, true, maxTextChars, &numberError)) {
        return err(numberError);
    }
    if (static_cast<quint64>(baseRevision) != surface->projectRevision()) {
        return err(QStringLiteral("B-roll opportunity context is stale."));
    }
    const QString contextSha = input.value(QStringLiteral("context_sha256")).toString().trimmed();
    if (contextSha.size() != 64) return err(QStringLiteral("context_sha256 must contain exactly 64 characters."));
    QJsonObject context;
    QString contextError;
    if (!buildCurrentContext(surface, static_cast<int>(maxCandidates), static_cast<int>(maxTextChars), context, &contextError)) return err(contextError);
    if (context.value(QStringLiteral("context_sha256")).toString() != contextSha) {
        return err(QStringLiteral("B-roll opportunity was not submitted against the exact current candidate context."));
    }
    QString validateError;
    QJsonObject result = validateVibeCutBrollOpportunity(context,
                                                         input.value(QStringLiteral("anchor_candidate_id")).toString(),
                                                         input.value(QStringLiteral("query")).toString(),
                                                         input.value(QStringLiteral("purpose")).toString(),
                                                         surface->projectRevision(), &validateError);
    if (!validateError.isEmpty()) return err(validateError);
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("context_max_candidates"), static_cast<int>(maxCandidates));
    result.insert(QStringLiteral("context_max_text_chars"), static_cast<int>(maxTextChars));
    return result;
}

void finalizeSearch(VibeCutJobManager *jobs,
                    VibeCutToolSurface *surface,
                    const QString &parentId,
                    const QString &childId,
                    quint64 baseRevision,
                    int maxCandidates,
                    int maxTextChars,
                    const QJsonObject &opportunity)
{
    if (!jobs || !surface) return;
    VibeCutJob parent;
    if (!jobs->job(parentId, parent) || parent.terminal()) return;
    if (parent.state == VibeCutJobState::CancelRequested) {
        jobs->requestCancel(childId);
        jobs->markCancelled(parentId, QStringLiteral("B-roll visual search cancelled."));
        return;
    }
    VibeCutJob child;
    if (!jobs->job(childId, child) || !child.terminal()) return;
    if (child.state != VibeCutJobState::Succeeded) {
        jobs->markFailed(parentId, QStringLiteral("B-roll SigLIP child search failed: %1").arg(child.message));
        return;
    }
    if (surface->projectRevision() != baseRevision) {
        jobs->markFailed(parentId, QStringLiteral("Project revision changed while B-roll visual search was running."));
        return;
    }

    QJsonObject currentContext;
    QString contextError;
    if (!buildCurrentContext(surface, maxCandidates, maxTextChars, currentContext, &contextError)) {
        jobs->markFailed(parentId, contextError);
        return;
    }
    if (currentContext.value(QStringLiteral("context_sha256")).toString() != opportunity.value(QStringLiteral("context_sha256")).toString()) {
        jobs->markFailed(parentId, QStringLiteral("B-roll transcript/evidence context changed while visual search was running."));
        return;
    }
    QString opportunityError;
    const QJsonObject currentOpportunity = validateVibeCutBrollOpportunity(
        currentContext,
        opportunity.value(QStringLiteral("anchor_candidate_id")).toString(),
        opportunity.value(QStringLiteral("query")).toString(),
        opportunity.value(QStringLiteral("purpose")).toString(),
        baseRevision, &opportunityError);
    if (!opportunityError.isEmpty() || currentOpportunity.value(QStringLiteral("opportunity_id")).toString() != opportunity.value(QStringLiteral("opportunity_id")).toString()) {
        jobs->markFailed(parentId, opportunityError.isEmpty() ? QStringLiteral("B-roll opportunity changed while search was running.") : opportunityError);
        return;
    }

    QJsonArray candidates;
    QHash<QString, bool> seenAnchors;
    for (const QJsonValue &value : child.result.value(QStringLiteral("hits")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject hit = value.toObject();
        const QString anchorId = hit.value(QStringLiteral("anchor_id")).toString().trimmed();
        const QString sourceId = hit.value(QStringLiteral("source_id")).toString().trimmed();
        const QString fingerprint = hit.value(QStringLiteral("source_fingerprint")).toString().trimmed();
        const int sampleFrame = hit.value(QStringLiteral("start_frame")).toInt(-1);
        const double similarity = hit.value(QStringLiteral("similarity")).toDouble(-2.0);
        if (anchorId.isEmpty() || seenAnchors.contains(anchorId) || sourceId.isEmpty() || fingerprint.isEmpty() ||
            sampleFrame < 0 || !std::isfinite(similarity) || similarity < -1.0 || similarity > 1.0) continue;
        seenAnchors.insert(anchorId, true);
        candidates.append(QJsonObject{{QStringLiteral("visual_anchor_id"), anchorId},
                                      {QStringLiteral("embedding_id"), hit.value(QStringLiteral("embedding_id"))},
                                      {QStringLiteral("source_id"), sourceId},
                                      {QStringLiteral("source_fingerprint"), fingerprint},
                                      {QStringLiteral("sample_frame"), sampleFrame},
                                      {QStringLiteral("similarity"), similarity},
                                      {QStringLiteral("score_semantics"), QStringLiteral("siglip_cosine_similarity_same_embedding_space_not_probability")},
                                      {QStringLiteral("metadata"), hit.value(QStringLiteral("metadata"))}});
    }
    const QJsonObject result{{QStringLiteral("kind"), QStringLiteral("broll_visual_candidates")},
                             {QStringLiteral("authority"), QStringLiteral("derived_ranking")},
                             {QStringLiteral("score_semantics"), QStringLiteral("current_siglip_visual_similarity_not_probability")},
                             {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)},
                             {QStringLiteral("context_sha256"), currentContext.value(QStringLiteral("context_sha256"))},
                             {QStringLiteral("context_max_candidates"), maxCandidates},
                             {QStringLiteral("context_max_text_chars"), maxTextChars},
                             {QStringLiteral("opportunity"), currentOpportunity},
                             {QStringLiteral("candidate_count"), candidates.size()},
                             {QStringLiteral("candidates"), candidates}};
    QString resultError;
    if (!jobs->setResult(parentId, result, &resultError)) {
        jobs->markFailed(parentId, QStringLiteral("B-roll candidate result was rejected: %1").arg(resultError));
        return;
    }
    jobs->markSucceeded(parentId, QStringLiteral("B-roll search returned %1 current visual candidate(s).").arg(candidates.size()));
}

QJsonObject startSearchTool(VibeCutTools *tools, VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!tools || !surface || !tools->jobManager()) return err(QStringLiteral("B-roll candidate search requires the VibeCut runtime."));
    QJsonObject opportunityResult = validateOpportunityTool(surface, input);
    if (!opportunityResult.value(QStringLiteral("ok")).toBool(false)) return opportunityResult;
    opportunityResult.remove(QStringLiteral("ok"));
    const int maxCandidates = opportunityResult.take(QStringLiteral("context_max_candidates")).toInt(200);
    const int maxTextChars = opportunityResult.take(QStringLiteral("context_max_text_chars")).toInt(600);

    QString numberError;
    qint64 limit = 12;
    if (!exactInteger(input, QStringLiteral("limit"), 1, 100, 12, false, limit, &numberError)) return err(numberError);
    double minSimilarity = -1.0;
    if (input.contains(QStringLiteral("min_similarity"))) {
        const QJsonValue raw = input.value(QStringLiteral("min_similarity"));
        if (!raw.isDouble() || !std::isfinite(raw.toDouble()) || raw.toDouble() < -1.0 || raw.toDouble() > 1.0) {
            return err(QStringLiteral("min_similarity must be a finite number between -1 and 1."));
        }
        minSimilarity = raw.toDouble();
    }
    const QString device = input.value(QStringLiteral("device")).toString(QStringLiteral("auto")).trimmed().toLower();
    if (device != QLatin1String("auto") && device != QLatin1String("cpu") && device != QLatin1String("cuda")) {
        return err(QStringLiteral("device must be auto, cpu, or cuda."));
    }

    const QJsonObject childStart = surface->invoke(QStringLiteral("semantic_search_visual"),
                                                    QJsonObject{{QStringLiteral("query"), opportunityResult.value(QStringLiteral("query"))},
                                                                {QStringLiteral("limit"), static_cast<int>(limit)},
                                                                {QStringLiteral("min_similarity"), minSimilarity},
                                                                {QStringLiteral("device"), device}});
    if (!childStart.value(QStringLiteral("ok")).toBool(false)) {
        return err(QStringLiteral("B-roll visual retrieval requires current SigLIP visual embeddings: %1")
                       .arg(childStart.value(QStringLiteral("error")).toString()));
    }
    const QString childId = childStart.value(QStringLiteral("job_id")).toString().trimmed();
    if (childId.isEmpty()) return err(QStringLiteral("SigLIP visual search returned no job id."));

    VibeCutJobManager *jobs = tools->jobManager();
    const quint64 baseRevision = surface->projectRevision();
    const QString parentId = jobs->createJob(QStringLiteral("broll_candidate_search"),
                                             QStringLiteral("B-roll candidates · %1").arg(opportunityResult.value(QStringLiteral("query")).toString().left(80)), true);
    jobs->markRunning(parentId, QStringLiteral("Searching current visual embeddings for B-roll candidates…"));
    QObject::connect(jobs, &VibeCutJobManager::jobChanged, jobs,
                     [jobs, surface, parentId, childId, baseRevision, maxCandidates, maxTextChars, opportunityResult](const QString &changedId) {
        if (changedId != childId) return;
        finalizeSearch(jobs, surface, parentId, childId, baseRevision, maxCandidates, maxTextChars, opportunityResult);
    });
    QObject::connect(jobs, &VibeCutJobManager::jobChanged, jobs,
                     [jobs, parentId, childId](const QString &changedId) {
        if (changedId != parentId) return;
        VibeCutJob parent;
        if (!jobs->job(parentId, parent) || parent.state != VibeCutJobState::CancelRequested) return;
        jobs->requestCancel(childId);
    });
    finalizeSearch(jobs, surface, parentId, childId, baseRevision, maxCandidates, maxTextChars, opportunityResult);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("started"), true},
                       {QStringLiteral("job_id"), parentId}, {QStringLiteral("visual_child_job_id"), childId},
                       {QStringLiteral("opportunity_id"), opportunityResult.value(QStringLiteral("opportunity_id"))},
                       {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)}};
}

QJsonObject resultTool(VibeCutTools *tools, const QJsonObject &input)
{
    if (!tools || !tools->jobManager()) return err(QStringLiteral("VibeCut JobManager is unavailable."));
    const QString id = input.value(QStringLiteral("job_id")).toString().trimmed();
    if (id.isEmpty()) return err(QStringLiteral("job_id must not be empty."));
    VibeCutJob job;
    if (!tools->jobManager()->job(id, job) || job.kind != QLatin1String("broll_candidate_search")) {
        return err(QStringLiteral("Unknown B-roll candidate-search job: %1").arg(id));
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

QJsonObject placementTool(VibeCutTools *tools, VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!tools || !surface || !tools->jobManager()) return err(QStringLiteral("B-roll placement validation requires the VibeCut runtime."));
    const QString jobId = input.value(QStringLiteral("search_job_id")).toString().trimmed();
    const QString anchorId = input.value(QStringLiteral("selected_visual_anchor_id")).toString().trimmed();
    if (jobId.isEmpty() || anchorId.isEmpty()) return err(QStringLiteral("search_job_id and selected_visual_anchor_id must not be empty."));
    VibeCutJob job;
    if (!tools->jobManager()->job(jobId, job) || job.kind != QLatin1String("broll_candidate_search") ||
        job.state != VibeCutJobState::Succeeded) {
        return err(QStringLiteral("search_job_id must identify a succeeded B-roll candidate-search job."));
    }
    const QJsonObject searchResult = job.result;
    const quint64 currentRevision = surface->projectRevision();
    if (searchResult.value(QStringLiteral("base_revision")).toDouble(-1) != static_cast<double>(currentRevision)) {
        return err(QStringLiteral("B-roll search result is stale relative to the current project revision."));
    }
    const int maxCandidates = searchResult.value(QStringLiteral("context_max_candidates")).toInt(200);
    const int maxTextChars = searchResult.value(QStringLiteral("context_max_text_chars")).toInt(600);
    QJsonObject currentContext;
    QString contextError;
    if (!buildCurrentContext(surface, maxCandidates, maxTextChars, currentContext, &contextError)) return err(contextError);
    if (currentContext.value(QStringLiteral("context_sha256")).toString() != searchResult.value(QStringLiteral("context_sha256")).toString()) {
        return err(QStringLiteral("B-roll context changed after candidate retrieval."));
    }
    QString placementError;
    QJsonObject result = buildVibeCutBrollPlacementProposal(searchResult.value(QStringLiteral("opportunity")).toObject(),
                                                            searchResult, anchorId, currentRevision, &placementError);
    if (!placementError.isEmpty()) return err(placementError);
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("search_job_id"), jobId);
    return result;
}
} // namespace

QJsonObject validateVibeCutBrollOpportunity(const QJsonObject &context,
                                            const QString &anchorCandidateId,
                                            const QString &query,
                                            const QString &purpose,
                                            quint64 currentRevision,
                                            QString *error)
{
    if (error) error->clear();
    const QString anchorId = anchorCandidateId.trimmed();
    const QString cleanQuery = query.simplified();
    const QString cleanPurpose = purpose.trimmed().toLower();
    if (anchorId.isEmpty() || anchorId.size() > 1024) {
        if (error) *error = QStringLiteral("B-roll anchor_candidate_id must contain 1..1024 characters.");
        return {};
    }
    if (cleanQuery.isEmpty() || cleanQuery.size() > 1024) {
        if (error) *error = QStringLiteral("B-roll visual query must contain 1..1024 characters.");
        return {};
    }
    if (!supportedPurpose(cleanPurpose)) {
        if (error) *error = QStringLiteral("B-roll purpose is unsupported.");
        return {};
    }

    // Reuse the canonical rough-cut validator to validate context identity,
    // revision, candidate existence and authoritative frame bounds.
    const QJsonObject proposal{{QStringLiteral("schema_version"), 1},
                               {QStringLiteral("authority"), QStringLiteral("proposal")},
                               {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                               {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                               {QStringLiteral("objective"), cleanQuery},
                               {QStringLiteral("selected_candidate_ids"), QJsonArray{anchorId}}};
    QString proposalError;
    const QJsonObject validated = validateVibeCutRoughCutProposal(context, proposal, currentRevision, &proposalError);
    if (!proposalError.isEmpty()) {
        if (error) *error = QStringLiteral("B-roll opportunity context was rejected: %1").arg(proposalError);
        return {};
    }
    const QJsonObject segment = validated.value(QStringLiteral("segments")).toArray().at(0).toObject();
    QJsonObject identity{{QStringLiteral("schema_version"), 1},
                         {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                         {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                         {QStringLiteral("anchor_candidate_id"), anchorId},
                         {QStringLiteral("query"), cleanQuery},
                         {QStringLiteral("purpose"), cleanPurpose}};
    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("authority"), QStringLiteral("proposal")},
                       {QStringLiteral("opportunity_id"), hashObject(identity)},
                       {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                       {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                       {QStringLiteral("anchor_candidate_id"), anchorId},
                       {QStringLiteral("target_start_frame"), segment.value(QStringLiteral("start_frame"))},
                       {QStringLiteral("target_end_frame"), segment.value(QStringLiteral("end_frame"))},
                       {QStringLiteral("target_duration_frames"), segment.value(QStringLiteral("duration_frames"))},
                       {QStringLiteral("target_text"), segment.value(QStringLiteral("text"))},
                       {QStringLiteral("target_text_sha256"), segment.value(QStringLiteral("text_sha256"))},
                       {QStringLiteral("target_source_id"), segment.value(QStringLiteral("source_id"))},
                       {QStringLiteral("target_source_fingerprint"), segment.value(QStringLiteral("source_fingerprint"))},
                       {QStringLiteral("query"), cleanQuery},
                       {QStringLiteral("purpose"), cleanPurpose},
                       {QStringLiteral("executable"), false},
                       {QStringLiteral("mutation_authority"), QStringLiteral("none")}};
}

QJsonObject buildVibeCutBrollPlacementProposal(const QJsonObject &opportunity,
                                               const QJsonObject &searchResult,
                                               const QString &selectedVisualAnchorId,
                                               quint64 currentRevision,
                                               QString *error)
{
    if (error) error->clear();
    if (opportunity.value(QStringLiteral("schema_version")).toInt(-1) != 1 ||
        opportunity.value(QStringLiteral("authority")).toString() != QLatin1String("proposal") ||
        opportunity.value(QStringLiteral("base_revision")).toDouble(-1) != static_cast<double>(currentRevision) ||
        opportunity.value(QStringLiteral("opportunity_id")).toString().size() != 64 ||
        opportunity.value(QStringLiteral("mutation_authority")).toString() != QLatin1String("none")) {
        if (error) *error = QStringLiteral("B-roll placement received a malformed or stale opportunity.");
        return {};
    }
    if (searchResult.value(QStringLiteral("kind")).toString() != QLatin1String("broll_visual_candidates") ||
        searchResult.value(QStringLiteral("authority")).toString() != QLatin1String("derived_ranking") ||
        searchResult.value(QStringLiteral("base_revision")).toDouble(-1) != static_cast<double>(currentRevision) ||
        searchResult.value(QStringLiteral("context_sha256")).toString() != opportunity.value(QStringLiteral("context_sha256")).toString() ||
        searchResult.value(QStringLiteral("opportunity")).toObject().value(QStringLiteral("opportunity_id")).toString() != opportunity.value(QStringLiteral("opportunity_id")).toString() ||
        !searchResult.value(QStringLiteral("candidates")).isArray()) {
        if (error) *error = QStringLiteral("B-roll placement requires the exact current visual-candidate result for this opportunity.");
        return {};
    }
    const QString wantedAnchor = selectedVisualAnchorId.trimmed();
    if (wantedAnchor.isEmpty()) {
        if (error) *error = QStringLiteral("selected_visual_anchor_id must not be empty.");
        return {};
    }
    QJsonObject selected;
    int matches = 0;
    for (const QJsonValue &value : searchResult.value(QStringLiteral("candidates")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject candidate = value.toObject();
        if (candidate.value(QStringLiteral("visual_anchor_id")).toString() == wantedAnchor) {
            selected = candidate;
            ++matches;
        }
    }
    if (matches != 1) {
        if (error) *error = QStringLiteral("Selected B-roll visual anchor must appear exactly once in the completed search result.");
        return {};
    }
    const QString sourceId = selected.value(QStringLiteral("source_id")).toString().trimmed();
    const QString fingerprint = selected.value(QStringLiteral("source_fingerprint")).toString().trimmed();
    const int sampleFrame = selected.value(QStringLiteral("sample_frame")).toInt(-1);
    const double similarity = selected.value(QStringLiteral("similarity")).toDouble(-2.0);
    if (sourceId.isEmpty() || fingerprint.isEmpty() || sampleFrame < 0 || !std::isfinite(similarity) || similarity < -1.0 || similarity > 1.0) {
        if (error) *error = QStringLiteral("Selected B-roll candidate has malformed source/provenance/similarity data.");
        return {};
    }
    const int targetStart = opportunity.value(QStringLiteral("target_start_frame")).toInt(-1);
    const int targetEnd = opportunity.value(QStringLiteral("target_end_frame")).toInt(-1);
    if (targetStart < 0 || targetEnd <= targetStart) {
        if (error) *error = QStringLiteral("B-roll opportunity target range is invalid.");
        return {};
    }
    QJsonObject identity{{QStringLiteral("schema_version"), 1},
                         {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                         {QStringLiteral("opportunity_id"), opportunity.value(QStringLiteral("opportunity_id"))},
                         {QStringLiteral("visual_anchor_id"), wantedAnchor},
                         {QStringLiteral("source_fingerprint"), fingerprint}};
    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("authority"), QStringLiteral("proposal")},
                       {QStringLiteral("placement_proposal_id"), hashObject(identity)},
                       {QStringLiteral("base_revision"), static_cast<qint64>(currentRevision)},
                       {QStringLiteral("context_sha256"), opportunity.value(QStringLiteral("context_sha256"))},
                       {QStringLiteral("opportunity_id"), opportunity.value(QStringLiteral("opportunity_id"))},
                       {QStringLiteral("anchor_candidate_id"), opportunity.value(QStringLiteral("anchor_candidate_id"))},
                       {QStringLiteral("purpose"), opportunity.value(QStringLiteral("purpose"))},
                       {QStringLiteral("query"), opportunity.value(QStringLiteral("query"))},
                       {QStringLiteral("target_start_frame"), targetStart},
                       {QStringLiteral("target_end_frame"), targetEnd},
                       {QStringLiteral("required_duration_frames"), targetEnd - targetStart},
                       {QStringLiteral("visual_anchor_id"), wantedAnchor},
                       {QStringLiteral("visual_embedding_id"), selected.value(QStringLiteral("embedding_id"))},
                       {QStringLiteral("visual_source_id"), sourceId},
                       {QStringLiteral("visual_source_fingerprint"), fingerprint},
                       {QStringLiteral("visual_sample_frame"), sampleFrame},
                       {QStringLiteral("visual_similarity"), similarity},
                       {QStringLiteral("score_semantics"), QStringLiteral("selected_current_siglip_candidate_not_quality_probability")},
                       {QStringLiteral("source_excerpt_resolution"), QStringLiteral("not_resolved")},
                       {QStringLiteral("source_excerpt_note"), QStringLiteral("The sampled frame is a retrieval reference/center only. A future governed execution translator must resolve and verify a source excerpt of the required duration; this proposal does not invent source in/out frames.")},
                       {QStringLiteral("executable"), false},
                       {QStringLiteral("mutation_authority"), QStringLiteral("none")},
                       {QStringLiteral("next_step"), QStringLiteral("review_then_resolve_source_excerpt_then_translate_to_governed_edit_plan")}};
}

bool registerVibeCutBrollTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("B-roll tools require native VibeCutTools/JobManager.");
        return false;
    }
    VibeCutToolSurface *surfacePtr = &surface;
    const QJsonObject contextProperties{
        {QStringLiteral("base_revision"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
        {QStringLiteral("context_sha256"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 64}, {QStringLiteral("maxLength"), 64}}},
        {QStringLiteral("context_max_candidates"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 300}}},
        {QStringLiteral("context_max_text_chars"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 64}, {QStringLiteral("maximum"), 2048}}},
        {QStringLiteral("anchor_candidate_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 1024}}},
        {QStringLiteral("query"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 1024}}},
        {QStringLiteral("purpose"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                 {QStringLiteral("enum"), QJsonArray{QStringLiteral("illustrate_subject"), QStringLiteral("show_process"),
                                                                                   QStringLiteral("establish_context"), QStringLiteral("cover_edit"),
                                                                                   QStringLiteral("show_detail"), QStringLiteral("show_result")}}}}};
    const QJsonArray contextRequired{QStringLiteral("base_revision"), QStringLiteral("context_sha256"),
                                     QStringLiteral("context_max_candidates"), QStringLiteral("context_max_text_chars"),
                                     QStringLiteral("anchor_candidate_id"), QStringLiteral("query"), QStringLiteral("purpose")};

    VibeCutToolPolicy opportunityPolicy;
    opportunityPolicy.name = QStringLiteral("broll_opportunity_validate");
    opportunityPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), opportunityPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Validate one proposal-only B-roll opportunity against the exact current rough-cut transcript context. The proposer supplies a canonical A-roll candidate id, bounded visual query and editorial purpose; target frame geometry/provenance is resolved from context and no edit authority is granted.")},
                                          {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                                       {QStringLiteral("properties"), contextProperties},
                                                                                       {QStringLiteral("required"), contextRequired},
                                                                                       {QStringLiteral("additionalProperties"), false}}}}},
                              opportunityPolicy, [surfacePtr](const QJsonObject &input) { return validateOpportunityTool(surfacePtr, input); }, error)) return false;

    QJsonObject searchProperties = contextProperties;
    searchProperties.insert(QStringLiteral("limit"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 100}});
    searchProperties.insert(QStringLiteral("min_similarity"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), -1.0}, {QStringLiteral("maximum"), 1.0}});
    searchProperties.insert(QStringLiteral("device"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                   {QStringLiteral("enum"), QJsonArray{QStringLiteral("auto"), QStringLiteral("cpu"), QStringLiteral("cuda")}}});
    VibeCutToolPolicy searchPolicy;
    searchPolicy.name = QStringLiteral("broll_candidate_search");
    searchPolicy.risk = VibeCutToolRisk::ReadOnly;
    searchPolicy.asynchronous = true;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), searchPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Asynchronously retrieve current SigLIP visual-frame candidates for one exact validated B-roll opportunity. Completion revalidates project revision and the full transcript context; returned similarity is ranking evidence, not probability.")},
                                          {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                                       {QStringLiteral("properties"), searchProperties},
                                                                                       {QStringLiteral("required"), contextRequired},
                                                                                       {QStringLiteral("additionalProperties"), false}}}}},
                              searchPolicy, [tools, surfacePtr](const QJsonObject &input) { return startSearchTool(tools, surfacePtr, input); }, error)) return false;

    const QJsonObject resultInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{{QStringLiteral("job_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("job_id")}},
                                  {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy resultPolicy;
    resultPolicy.name = QStringLiteral("broll_candidate_result");
    resultPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), resultPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Read one B-roll visual candidate-search job/result.")},
                                          {QStringLiteral("input_schema"), resultInput}},
                              resultPolicy, [tools](const QJsonObject &input) { return resultTool(tools, input); }, error)) return false;

    const QJsonObject placementInput{{QStringLiteral("type"), QStringLiteral("object")},
                                     {QStringLiteral("properties"), QJsonObject{
                                         {QStringLiteral("search_job_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                         {QStringLiteral("selected_visual_anchor_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                     {QStringLiteral("required"), QJsonArray{QStringLiteral("search_job_id"), QStringLiteral("selected_visual_anchor_id")}},
                                     {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy placementPolicy;
    placementPolicy.name = QStringLiteral("broll_placement_plan_validate");
    placementPolicy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), placementPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Validate a reviewable B-roll placement proposal by selecting exactly one visual anchor returned by a succeeded current B-roll search. The proposal resolves the A-roll cover range and sampled visual reference, but intentionally does not invent a source excerpt or execute an edit.")},
                                            {QStringLiteral("input_schema"), placementInput}},
                                placementPolicy, [tools, surfacePtr](const QJsonObject &input) { return placementTool(tools, surfacePtr, input); }, error);
}
