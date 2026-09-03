/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuthybridsearch.h"

#include "vibecutjobmanager.h"
#include "vibecutmediaindex.h"
#include "vibecuttools.h"
#include "vibecuttoolsurface.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {
struct RankedHit {
    QString id;
    QString kind;
    QString text;
    int startFrame = -1;
    int endFrame = -1;
    double lexical = 0.0;
    double semantic = 0.0;
    bool hasLexical = false;
    bool hasSemantic = false;
    QJsonObject metadata;
};

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

double lexicalUnit(int score)
{
    if (score <= 0) return 0.0;
    // Bounded monotonic transform; preserves the lexical index's exact/token
    // ordering without pretending its integer score is a probability.
    return qBound(0.0, static_cast<double>(score) / (static_cast<double>(score) + 250.0), 1.0);
}

QJsonObject startHybrid(VibeCutTools *tools, VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!tools || !surface || !tools->jobManager()) return err(QStringLiteral("Hybrid search requires the VibeCut runtime."));
    const QString query = input.value(QStringLiteral("query")).toString().trimmed();
    if (query.isEmpty() || query.size() > 2048) return err(QStringLiteral("query must contain 1..2048 characters."));
    const int limit = qBound(1, input.value(QStringLiteral("limit")).toInt(25), 100);
    const double minScore = qBound(0.0, input.value(QStringLiteral("min_score")).toDouble(0.0), 1.0);

    VibeCutMediaIndex index;
    QString indexError;
    if (!index.rebuildFromCurrentProject(&indexError)) return err(indexError);
    const QList<VibeCutMediaSearchHit> lexicalHits = index.search(query, 100);
    QHash<QString, VibeCutMediaDocument> documents;
    for (const VibeCutMediaDocument &document : index.documents()) documents.insert(document.id, document);

    const QJsonObject semanticStart = surface->invoke(QStringLiteral("semantic_search_text"),
                                                      QJsonObject{{QStringLiteral("query"), query},
                                                                  {QStringLiteral("limit"), 100},
                                                                  {QStringLiteral("min_similarity"), -1.0},
                                                                  {QStringLiteral("device"), input.value(QStringLiteral("device")).toString(QStringLiteral("auto"))}});
    if (!semanticStart.value(QStringLiteral("ok")).toBool(false)) {
        return err(QStringLiteral("Hybrid search requires current MiniLM embeddings: %1")
                       .arg(semanticStart.value(QStringLiteral("error")).toString()));
    }
    const QString childId = semanticStart.value(QStringLiteral("job_id")).toString().trimmed();
    if (childId.isEmpty()) return err(QStringLiteral("MiniLM semantic search did not return a job id."));

    VibeCutJobManager *jobs = tools->jobManager();
    const quint64 baseRevision = surface->projectRevision();
    const QString parentId = jobs->createJob(QStringLiteral("semantic_hybrid_search"),
                                             QStringLiteral("Hybrid media search · %1").arg(query.left(80)), true);
    jobs->markRunning(parentId, QStringLiteral("Waiting for current MiniLM ranking and fusing lexical evidence…"));

    QObject::connect(jobs, &VibeCutJobManager::jobChanged, jobs,
                     [jobs, surface, parentId, childId, baseRevision, query, limit, minScore, lexicalHits, documents]
                     (const QString &changedId) {
        if (changedId != childId) return;
        VibeCutJob parent;
        if (!jobs->job(parentId, parent) || parent.terminal()) return;
        if (parent.state == VibeCutJobState::CancelRequested) {
            QString cancelError;
            jobs->requestCancel(childId, &cancelError);
            jobs->markCancelled(parentId, QStringLiteral("Hybrid search cancelled."));
            return;
        }

        VibeCutJob child;
        if (!jobs->job(childId, child) || !child.terminal()) return;
        if (surface->projectRevision() != baseRevision) {
            jobs->markFailed(parentId, QStringLiteral("Project revision changed while hybrid search was running; refusing stale ranking."));
            return;
        }
        if (child.state != VibeCutJobState::Succeeded) {
            jobs->markFailed(parentId, QStringLiteral("MiniLM semantic child search failed: %1").arg(child.message));
            return;
        }

        QHash<QString, RankedHit> byId;
        for (const VibeCutMediaSearchHit &hit : lexicalHits) {
            RankedHit item;
            item.id = hit.document.id;
            item.kind = hit.document.kind;
            item.text = hit.document.text;
            item.startFrame = hit.document.startFrame;
            item.endFrame = hit.document.endFrame;
            item.lexical = lexicalUnit(hit.score);
            item.hasLexical = true;
            item.metadata = hit.document.metadata;
            byId.insert(item.id, item);
        }

        int staleSemanticSkipped = 0;
        const QJsonArray semanticHits = child.result.value(QStringLiteral("hits")).toArray();
        for (const QJsonValue &value : semanticHits) {
            if (!value.isObject()) continue;
            const QJsonObject object = value.toObject();
            if (!object.value(QStringLiteral("anchor_current")).toBool(false)) {
                ++staleSemanticSkipped;
                continue;
            }
            const QString id = object.value(QStringLiteral("anchor_id")).toString();
            if (id.isEmpty() || !documents.contains(id)) continue;
            const double similarity = object.value(QStringLiteral("similarity")).toDouble(-1.0);
            if (!std::isfinite(similarity)) continue;
            RankedHit item = byId.value(id);
            const VibeCutMediaDocument document = documents.value(id);
            item.id = id;
            item.kind = document.kind;
            item.text = document.text;
            item.startFrame = document.startFrame;
            item.endFrame = document.endFrame;
            item.semantic = qBound(0.0, similarity, 1.0);
            item.hasSemantic = true;
            if (item.metadata.isEmpty()) item.metadata = document.metadata;
            byId.insert(id, item);
        }

        QList<QJsonObject> ranked;
        for (auto it = byId.constBegin(); it != byId.constEnd(); ++it) {
            const RankedHit &item = it.value();
            const double semanticWeight = item.hasSemantic ? 0.55 : 0.0;
            const double lexicalWeight = item.hasLexical ? 0.45 : 0.0;
            const double weight = semanticWeight + lexicalWeight;
            if (weight <= 0.0) continue;
            const double score = qBound(0.0,
                                        (semanticWeight * item.semantic + lexicalWeight * item.lexical) / weight,
                                        1.0);
            if (score < minScore) continue;
            ranked.append(QJsonObject{{QStringLiteral("anchor_id"), item.id},
                                      {QStringLiteral("kind"), item.kind},
                                      {QStringLiteral("text"), item.text.left(2048)},
                                      {QStringLiteral("start_frame"), item.startFrame},
                                      {QStringLiteral("end_frame"), item.endFrame},
                                      {QStringLiteral("hybrid_score"), score},
                                      {QStringLiteral("lexical_available"), item.hasLexical},
                                      {QStringLiteral("lexical_component"), item.lexical},
                                      {QStringLiteral("semantic_available"), item.hasSemantic},
                                      {QStringLiteral("semantic_component"), item.semantic},
                                      {QStringLiteral("metadata"), item.metadata}});
        }
        std::sort(ranked.begin(), ranked.end(), [](const QJsonObject &a, const QJsonObject &b) {
            const double aScore = a.value(QStringLiteral("hybrid_score")).toDouble();
            const double bScore = b.value(QStringLiteral("hybrid_score")).toDouble();
            if (aScore != bScore) return aScore > bScore;
            const int aStart = a.value(QStringLiteral("start_frame")).toInt(-1);
            const int bStart = b.value(QStringLiteral("start_frame")).toInt(-1);
            if (aStart != bStart) return aStart < bStart;
            return a.value(QStringLiteral("anchor_id")).toString() < b.value(QStringLiteral("anchor_id")).toString();
        });
        while (ranked.size() > limit) ranked.removeLast();
        QJsonArray hits;
        for (const QJsonObject &object : ranked) hits.append(object);

        const QJsonObject result{{QStringLiteral("kind"), QStringLiteral("semantic_hybrid_search")},
                                 {QStringLiteral("query"), query},
                                 {QStringLiteral("authority"), QStringLiteral("derived_ranking")},
                                 {QStringLiteral("score_semantics"), QStringLiteral("weighted_current_lexical_and_minilm_similarity_not_probability")},
                                 {QStringLiteral("semantic_weight_when_available"), 0.55},
                                 {QStringLiteral("lexical_weight_when_available"), 0.45},
                                 {QStringLiteral("stale_semantic_hits_skipped"), staleSemanticSkipped},
                                 {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)},
                                 {QStringLiteral("hit_count"), hits.size()},
                                 {QStringLiteral("hits"), hits}};
        QString resultError;
        if (!jobs->setResult(parentId, result, &resultError)) {
            jobs->markFailed(parentId, QStringLiteral("Hybrid ranking completed but its structured result was rejected: %1").arg(resultError));
            return;
        }
        jobs->markSucceeded(parentId, QStringLiteral("Hybrid current-only search produced %1 ranked hit(s).").arg(hits.size()));
    });

    QObject::connect(jobs, &VibeCutJobManager::jobChanged, jobs,
                     [jobs, parentId, childId](const QString &changedId) {
        if (changedId != parentId) return;
        VibeCutJob parent;
        if (!jobs->job(parentId, parent) || parent.state != VibeCutJobState::CancelRequested) return;
        QString error;
        jobs->requestCancel(childId, &error);
    });

    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("started"), true},
                       {QStringLiteral("job_id"), parentId},
                       {QStringLiteral("semantic_child_job_id"), childId},
                       {QStringLiteral("base_revision"), static_cast<qint64>(baseRevision)},
                       {QStringLiteral("lexical_hit_count"), lexicalHits.size()}};
}

QJsonObject resultTool(VibeCutTools *tools, const QJsonObject &input)
{
    if (!tools || !tools->jobManager()) return err(QStringLiteral("VibeCut JobManager is unavailable."));
    const QString id = input.value(QStringLiteral("job_id")).toString().trimmed();
    if (id.isEmpty()) return err(QStringLiteral("job_id must not be empty."));
    VibeCutJob job;
    if (!tools->jobManager()->job(id, job) || job.kind != QLatin1String("semantic_hybrid_search")) {
        return err(QStringLiteral("Unknown hybrid-search job: %1").arg(id));
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

bool registerVibeCutHybridSearchTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutTools *tools = surface.baseTools();
    if (!tools) {
        if (error) *error = QStringLiteral("Hybrid search requires native VibeCutTools/JobManager.");
        return false;
    }
    VibeCutToolSurface *surfacePtr = &surface;
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("query"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 2048}}},
                                {QStringLiteral("limit"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 100}}},
                                {QStringLiteral("min_score"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                {QStringLiteral("device"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                        {QStringLiteral("enum"), QJsonArray{QStringLiteral("auto"), QStringLiteral("cpu"), QStringLiteral("cuda")}}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("query")}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy searchPolicy;
    searchPolicy.name = QStringLiteral("media_search_hybrid");
    searchPolicy.risk = VibeCutToolRisk::ReadOnly;
    searchPolicy.asynchronous = true;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), searchPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Fuse the current canonical lexical media index with the pinned MiniLM semantic ranking. Semantic hits whose source/text anchors are no longer current are excluded before fusion. Returns a job id and an explicitly non-probabilistic derived ranking.")},
                                          {QStringLiteral("input_schema"), input}},
                              searchPolicy, [tools, surfacePtr](const QJsonObject &args) { return startHybrid(tools, surfacePtr, args); }, error)) return false;

    const QJsonObject resultInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{{QStringLiteral("job_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("job_id")}},
                                  {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy resultPolicy;
    resultPolicy.name = QStringLiteral("media_search_hybrid_result");
    resultPolicy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), resultPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Read the state/result of one current-only hybrid lexical+MiniLM media-search job.")},
                                            {QStringLiteral("input_schema"), resultInput}},
                                resultPolicy, [tools](const QJsonObject &args) { return resultTool(tools, args); }, error);
}
