/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutroughcutalternatives.h"
#include "vibecut/vibecutroughcutsynthesis.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
VibeCutMediaDocument doc(const QString &id, int start, int end, const QString &text)
{
    VibeCutMediaDocument document;
    document.id = id;
    document.kind = QStringLiteral("transcript");
    document.startFrame = start;
    document.endFrame = end;
    document.text = text;
    document.metadata = QJsonObject{{QStringLiteral("evidence_origin"), QStringLiteral("subtitle_track")}};
    return document;
}

QJsonObject rankingCandidate(const QString &id, double score)
{
    return QJsonObject{{QStringLiteral("candidate_id"), id},
                       {QStringLiteral("objective_relevance_score"), score}};
}

QJsonObject rankingFor(const QJsonObject &context, const QString &objective, const QJsonArray &candidates)
{
    return QJsonObject{{QStringLiteral("kind"), QStringLiteral("rough_cut_objective_rank")},
                       {QStringLiteral("authority"), QStringLiteral("derived_ranking")},
                       {QStringLiteral("score_semantics"), QStringLiteral("current_hybrid_relevance_not_probability")},
                       {QStringLiteral("base_revision"), context.value(QStringLiteral("base_revision"))},
                       {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                       {QStringLiteral("objective"), objective},
                       {QStringLiteral("candidates"), candidates}};
}

QJsonObject alternative(const QString &id, const QJsonArray &selected)
{
    return QJsonObject{{QStringLiteral("alternative_id"), id},
                       {QStringLiteral("selected_candidate_ids"), selected}};
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

TEST_CASE("rough-cut alternatives are ranked by a disclosed non-probabilistic rubric", "[vibecut][rough-cut][alternatives]")
{
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(
        {doc(QStringLiteral("a"), 0, 40, QStringLiteral("opening")),
         doc(QStringLiteral("b"), 50, 90, QStringLiteral("main point")),
         doc(QStringLiteral("c"), 100, 140, QStringLiteral("detail"))},
        9, 20, 600, &error);
    REQUIRE(error.isEmpty());
    const QJsonObject ranking = rankingFor(context, QStringLiteral("concise engine story"),
                                           QJsonArray{rankingCandidate(QStringLiteral("a"), 0.95),
                                                      rankingCandidate(QStringLiteral("b"), 0.85),
                                                      rankingCandidate(QStringLiteral("c"), 0.60)});

    const QJsonArray alternatives{
        alternative(QStringLiteral("chronological"), QJsonArray{QStringLiteral("a"), QStringLiteral("b")}),
        alternative(QStringLiteral("reordered"), QJsonArray{QStringLiteral("b"), QStringLiteral("a")}),
        alternative(QStringLiteral("weaker"), QJsonArray{QStringLiteral("c")}),
    };
    const QJsonObject result = compareVibeCutRoughCutAlternatives(context, ranking, alternatives, 9, &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("authority")).toString() == QStringLiteral("derived_comparison"));
    CHECK(result.value(QStringLiteral("score_semantics")).toString() == QStringLiteral("fixed_transparent_editorial_comparison_not_probability"));
    CHECK_FALSE(result.value(QStringLiteral("executable")).toBool(true));
    CHECK(result.value(QStringLiteral("mutation_authority")).toString() == QStringLiteral("none"));
    CHECK(result.value(QStringLiteral("top_ranked_alternative_id")).toString() == QStringLiteral("chronological"));
    const QJsonObject weights = result.value(QStringLiteral("weights")).toObject();
    CHECK(weights.value(QStringLiteral("objective_relevance")).toDouble() == Approx(0.60));
    CHECK(weights.value(QStringLiteral("retrieval_coverage")).toDouble() == Approx(0.15));
    CHECK(weights.value(QStringLiteral("chronology")).toDouble() == Approx(0.10));
    CHECK(weights.value(QStringLiteral("overlap_cleanliness")).toDouble() == Approx(0.10));
    CHECK(weights.value(QStringLiteral("provenance_coverage")).toDouble() == Approx(0.05));

    const QJsonArray scored = result.value(QStringLiteral("alternatives")).toArray();
    REQUIRE(scored.size() == 3);
    CHECK(scored.at(0).toObject().value(QStringLiteral("alternative_id")).toString() == QStringLiteral("chronological"));
    CHECK(scored.at(0).toObject().value(QStringLiteral("rank")).toInt() == 1);
    CHECK(scored.at(1).toObject().value(QStringLiteral("alternative_id")).toString() == QStringLiteral("reordered"));
    CHECK(scored.at(1).toObject().value(QStringLiteral("chronology_component")).toDouble() == Approx(0.5));
}

TEST_CASE("rough-cut alternative comparison exposes retrieval coverage rather than treating missing relevance as zero evidence", "[vibecut][rough-cut][alternatives][coverage]")
{
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(
        {doc(QStringLiteral("a"), 0, 40, QStringLiteral("one")),
         doc(QStringLiteral("b"), 50, 90, QStringLiteral("two"))},
        4, 20, 600, &error);
    REQUIRE(error.isEmpty());
    const QJsonObject ranking = rankingFor(context, QStringLiteral("objective"),
                                           QJsonArray{rankingCandidate(QStringLiteral("a"), 0.9)});
    const QJsonObject result = compareVibeCutRoughCutAlternatives(
        context, ranking,
        QJsonArray{alternative(QStringLiteral("covered"), QJsonArray{QStringLiteral("a")}),
                   alternative(QStringLiteral("partial"), QJsonArray{QStringLiteral("a"), QStringLiteral("b")})},
        4, &error);
    REQUIRE(error.isEmpty());
    const QJsonArray scored = result.value(QStringLiteral("alternatives")).toArray();
    REQUIRE(scored.size() == 2);
    QJsonObject partial;
    for (const QJsonValue &value : scored) {
        if (value.toObject().value(QStringLiteral("alternative_id")).toString() == QLatin1String("partial")) partial = value.toObject();
    }
    REQUIRE_FALSE(partial.isEmpty());
    CHECK(partial.value(QStringLiteral("mean_objective_relevance_available")).toDouble() == Approx(0.9));
    CHECK(partial.value(QStringLiteral("objective_relevance_coverage")).toDouble() == Approx(0.5));
}

TEST_CASE("rough-cut alternative comparison fails closed on stale ranking unknown candidates and duplicate alternative ids", "[vibecut][rough-cut][alternatives][integrity]")
{
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(
        {doc(QStringLiteral("a"), 0, 40, QStringLiteral("one")),
         doc(QStringLiteral("b"), 50, 90, QStringLiteral("two"))},
        6, 20, 600, &error);
    REQUIRE(error.isEmpty());
    QJsonObject ranking = rankingFor(context, QStringLiteral("objective"),
                                     QJsonArray{rankingCandidate(QStringLiteral("a"), 0.9),
                                                rankingCandidate(QStringLiteral("b"), 0.8)});

    CHECK(compareVibeCutRoughCutAlternatives(
              context, ranking,
              QJsonArray{alternative(QStringLiteral("x"), QJsonArray{QStringLiteral("a")}),
                         alternative(QStringLiteral("y"), QJsonArray{QStringLiteral("b")})},
              7, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("stale"), Qt::CaseInsensitive));

    error.clear();
    CHECK(compareVibeCutRoughCutAlternatives(
              context, ranking,
              QJsonArray{alternative(QStringLiteral("x"), QJsonArray{QStringLiteral("invented")}),
                         alternative(QStringLiteral("y"), QJsonArray{QStringLiteral("b")})},
              6, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("unknown"), Qt::CaseInsensitive));

    error.clear();
    CHECK(compareVibeCutRoughCutAlternatives(
              context, ranking,
              QJsonArray{alternative(QStringLiteral("same"), QJsonArray{QStringLiteral("a")}),
                         alternative(QStringLiteral("same"), QJsonArray{QStringLiteral("b")})},
              6, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("unique"), Qt::CaseInsensitive));

    error.clear();
    ranking.insert(QStringLiteral("context_sha256"), QString(64, QLatin1Char('0')));
    CHECK(compareVibeCutRoughCutAlternatives(
              context, ranking,
              QJsonArray{alternative(QStringLiteral("x"), QJsonArray{QStringLiteral("a")}),
                         alternative(QStringLiteral("y"), QJsonArray{QStringLiteral("b")})},
              6, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("exact"), Qt::CaseInsensitive));
}

TEST_CASE("rough-cut alternative tool is read-only and accepts candidate ids rather than edit geometry", "[vibecut][rough-cut][alternatives][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("rough_cut_alternatives_compare")));
    const VibeCutToolPolicy policy = policies.value(QStringLiteral("rough_cut_alternatives_compare"));
    CHECK(policy.risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(policy.asynchronous);
    CHECK_FALSE(policy.mutatesProject);

    const QJsonObject schema = schemaByName(surface, QStringLiteral("rough_cut_alternatives_compare"));
    REQUIRE_FALSE(schema.isEmpty());
    const QJsonObject properties = schema.value(QStringLiteral("input_schema")).toObject()
                                       .value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("objective_job_id")));
    CHECK(properties.contains(QStringLiteral("alternatives")));
    CHECK_FALSE(properties.contains(QStringLiteral("start_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("end_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("source_path")));
    CHECK_FALSE(properties.contains(QStringLiteral("operations")));
    CHECK_FALSE(properties.contains(QStringLiteral("weights")));
}
