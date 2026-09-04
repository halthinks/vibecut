/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutroughcutrelevance.h"
#include "vibecut/vibecutroughcutsynthesis.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
VibeCutMediaDocument candidateDoc(const QString &id, int start, int end, const QString &text,
                                  const QString &source = QString(), const QString &fingerprint = QString())
{
    VibeCutMediaDocument document;
    document.id = id;
    document.kind = source.isEmpty() ? QStringLiteral("transcript") : QStringLiteral("transcript_segment");
    document.startFrame = start;
    document.endFrame = end;
    document.text = text;
    document.metadata = QJsonObject{{QStringLiteral("evidence_origin"), source.isEmpty()
                                                                            ? QStringLiteral("subtitle_track")
                                                                            : QStringLiteral("extractor")}};
    if (!source.isEmpty()) document.metadata.insert(QStringLiteral("source_id"), source);
    if (!fingerprint.isEmpty()) document.metadata.insert(QStringLiteral("source_fingerprint"), fingerprint);
    return document;
}

QJsonObject hybridHit(const QString &id, const QString &kind, int start, int end, double score,
                      const QString &source = QString(), const QString &fingerprint = QString())
{
    QJsonObject metadata;
    if (!source.isEmpty()) metadata.insert(QStringLiteral("source_id"), source);
    if (!fingerprint.isEmpty()) metadata.insert(QStringLiteral("source_fingerprint"), fingerprint);
    return QJsonObject{{QStringLiteral("anchor_id"), id},
                       {QStringLiteral("kind"), kind},
                       {QStringLiteral("start_frame"), start},
                       {QStringLiteral("end_frame"), end},
                       {QStringLiteral("hybrid_score"), score},
                       {QStringLiteral("lexical_available"), true},
                       {QStringLiteral("lexical_component"), score - 0.05},
                       {QStringLiteral("semantic_available"), true},
                       {QStringLiteral("semantic_component"), score + 0.02},
                       {QStringLiteral("metadata"), metadata}};
}

QJsonObject hybridResult(int revision, const QJsonArray &hits)
{
    return QJsonObject{{QStringLiteral("kind"), QStringLiteral("semantic_hybrid_search")},
                       {QStringLiteral("authority"), QStringLiteral("derived_ranking")},
                       {QStringLiteral("score_semantics"), QStringLiteral("weighted_current_lexical_and_minilm_similarity_not_probability")},
                       {QStringLiteral("base_revision"), revision},
                       {QStringLiteral("hits"), hits}};
}

QJsonObject schemaByName(const VibeCutToolSurface &surface, const QString &name)
{
    for (const QJsonValue &value : surface.schemas()) {
        const QJsonObject schema = value.toObject();
        if (schema.value(QStringLiteral("name")).toString() == name) return schema;
    }
    return {};
}
}

TEST_CASE("rough-cut objective ranking emits only exact current context candidates", "[vibecut][rough-cut][relevance]")
{
    QList<VibeCutMediaDocument> documents{
        candidateDoc(QStringLiteral("a"), 10, 40, QStringLiteral("engine efficiency answer"),
                     QStringLiteral("timeline:a"), QStringLiteral("fp-a")),
        candidateDoc(QStringLiteral("b"), 50, 90, QStringLiteral("battery discussion"),
                     QStringLiteral("timeline:b"), QStringLiteral("fp-b")),
    };
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(documents, 11, 20, 600, &error);
    REQUIRE(error.isEmpty());

    const QJsonArray hits{
        hybridHit(QStringLiteral("a"), QStringLiteral("transcript_segment"), 10, 40, 0.91,
                  QStringLiteral("timeline:a"), QStringLiteral("fp-a")),
        hybridHit(QStringLiteral("ocr:outside"), QStringLiteral("ocr_text"), 20, 21, 0.99),
        hybridHit(QStringLiteral("b"), QStringLiteral("transcript_segment"), 50, 90, 0.88,
                  QStringLiteral("timeline:b"), QStringLiteral("stale-fingerprint")),
    };
    const QJsonObject ranked = rankVibeCutRoughCutObjectiveHits(context, hybridResult(11, hits), 20, 0.0, &error);
    REQUIRE(error.isEmpty());
    CHECK(ranked.value(QStringLiteral("authority")).toString() == QStringLiteral("derived_ranking"));
    CHECK(ranked.value(QStringLiteral("score_semantics")).toString() == QStringLiteral("current_hybrid_relevance_not_probability"));
    CHECK_FALSE(ranked.value(QStringLiteral("executable")).toBool(true));
    CHECK(ranked.value(QStringLiteral("mutation_authority")).toString() == QStringLiteral("none"));
    CHECK(ranked.value(QStringLiteral("candidate_count")).toInt() == 1);
    CHECK(ranked.value(QStringLiteral("non_candidate_hits_skipped")).toInt() == 1);
    CHECK(ranked.value(QStringLiteral("provenance_mismatch_hits_skipped")).toInt() == 1);
    const QJsonArray output = ranked.value(QStringLiteral("candidates")).toArray();
    REQUIRE(output.size() == 1);
    CHECK(output.at(0).toObject().value(QStringLiteral("candidate_id")).toString() == QStringLiteral("a"));
    CHECK(output.at(0).toObject().value(QStringLiteral("objective_relevance_score")).toDouble() == Approx(0.91));
}

TEST_CASE("rough-cut objective ranking enforces revision score threshold limit and deterministic order", "[vibecut][rough-cut][relevance][ranking]")
{
    QList<VibeCutMediaDocument> documents{
        candidateDoc(QStringLiteral("a"), 10, 40, QStringLiteral("alpha")),
        candidateDoc(QStringLiteral("b"), 50, 90, QStringLiteral("beta")),
        candidateDoc(QStringLiteral("c"), 100, 140, QStringLiteral("gamma")),
    };
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(documents, 5, 20, 600, &error);
    REQUIRE(error.isEmpty());

    CHECK(rankVibeCutRoughCutObjectiveHits(context,
                                           hybridResult(6, QJsonArray{hybridHit(QStringLiteral("a"), QStringLiteral("transcript"), 10, 40, 0.9)}),
                                           20, 0.0, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("revision"), Qt::CaseInsensitive));

    error.clear();
    const QJsonArray hits{
        hybridHit(QStringLiteral("c"), QStringLiteral("transcript"), 100, 140, 0.72),
        hybridHit(QStringLiteral("a"), QStringLiteral("transcript"), 10, 40, 0.94),
        hybridHit(QStringLiteral("b"), QStringLiteral("transcript"), 50, 90, 0.83),
    };
    const QJsonObject ranked = rankVibeCutRoughCutObjectiveHits(context, hybridResult(5, hits), 2, 0.80, &error);
    REQUIRE(error.isEmpty());
    CHECK(ranked.value(QStringLiteral("candidate_count")).toInt() == 2);
    CHECK(ranked.value(QStringLiteral("below_score_hits_skipped")).toInt() == 1);
    const QJsonArray output = ranked.value(QStringLiteral("candidates")).toArray();
    REQUIRE(output.size() == 2);
    CHECK(output.at(0).toObject().value(QStringLiteral("candidate_id")).toString() == QStringLiteral("a"));
    CHECK(output.at(1).toObject().value(QStringLiteral("candidate_id")).toString() == QStringLiteral("b"));
}

TEST_CASE("rough-cut relevance tools expose bounded read-only proposal support", "[vibecut][rough-cut][relevance][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("rough_cut_objective_rank")));
    REQUIRE(policies.contains(QStringLiteral("rough_cut_objective_result")));
    CHECK(policies.value(QStringLiteral("rough_cut_objective_rank")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("rough_cut_objective_rank")).asynchronous);
    CHECK_FALSE(policies.value(QStringLiteral("rough_cut_objective_rank")).mutatesProject);
    CHECK(policies.value(QStringLiteral("rough_cut_objective_result")).risk == VibeCutToolRisk::ReadOnly);

    const QJsonObject schema = schemaByName(surface, QStringLiteral("rough_cut_objective_rank"));
    REQUIRE_FALSE(schema.isEmpty());
    const QJsonObject properties = schema.value(QStringLiteral("input_schema")).toObject()
                                       .value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("objective")));
    CHECK(properties.contains(QStringLiteral("context_sha256")));
    CHECK_FALSE(properties.contains(QStringLiteral("source_path")));
    CHECK_FALSE(properties.contains(QStringLiteral("start_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("end_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("model")));
    CHECK_FALSE(properties.contains(QStringLiteral("vector")));
    CHECK_FALSE(properties.contains(QStringLiteral("operations")));
}
