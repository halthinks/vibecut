/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutretrievaleval.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

#include <cmath>

namespace {
QJsonObject schemaByName(const VibeCutToolSurface &surface, const QString &name)
{
    for (const QJsonValue &value : surface.schemas()) {
        const QJsonObject schema = value.toObject();
        if (schema.value(QStringLiteral("name")).toString() == name) return schema;
    }
    return {};
}
}

TEST_CASE("retrieval evaluation reports top-k agreement against explicit relevance reference", "[vibecut][retrieval-eval]")
{
    QString error;
    const QJsonObject result = evaluateVibeCutRetrievalRanking(
        QJsonArray{QStringLiteral("a"), QStringLiteral("c")},
        QJsonArray{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")}, 3, &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("precision_at_k")).toDouble() == Approx(2.0 / 3.0).epsilon(1e-9));
    CHECK(result.value(QStringLiteral("recall_at_k")).toDouble() == Approx(1.0));
    CHECK(result.value(QStringLiteral("average_precision_at_k")).toDouble() == Approx(5.0 / 6.0).epsilon(1e-9));
    const double expectedNdcg = 1.5 / (1.0 + 1.0 / std::log2(3.0));
    CHECK(result.value(QStringLiteral("ndcg_at_k_binary_relevance")).toDouble() == Approx(expectedNdcg).epsilon(1e-9));
    CHECK(result.value(QStringLiteral("reciprocal_rank")).toDouble() == Approx(1.0));
    CHECK(result.value(QStringLiteral("recall_over_full_ranked_list")).toDouble() == Approx(1.0));
    CHECK_FALSE(result.value(QStringLiteral("quality_claim")).toBool(true));
    CHECK(result.value(QStringLiteral("evaluation_semantics")).toString().contains(QStringLiteral("not_semantic_truth")));
}

TEST_CASE("retrieval precision at k penalizes unfilled top-k slots", "[vibecut][retrieval-eval][short-list]")
{
    QString error;
    const QJsonObject result = evaluateVibeCutRetrievalRanking(
        QJsonArray{QStringLiteral("a"), QStringLiteral("b")},
        QJsonArray{QStringLiteral("a")}, 2, &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("returned_within_k")).toInt() == 1);
    CHECK(result.value(QStringLiteral("precision_at_k")).toDouble() == Approx(0.5));
    CHECK(result.value(QStringLiteral("recall_at_k")).toDouble() == Approx(0.5));
    CHECK(result.value(QStringLiteral("average_precision_at_k")).toDouble() == Approx(0.5));
    CHECK(result.value(QStringLiteral("precision_at_k_denominator")).toString().contains(QStringLiteral("unfilled_slots")));
}

TEST_CASE("retrieval evaluation distinguishes first-hit rank and missing relevant ids", "[vibecut][retrieval-eval][rank]")
{
    QString error;
    const QJsonObject result = evaluateVibeCutRetrievalRanking(
        QJsonArray{QStringLiteral("a"), QStringLiteral("z")},
        QJsonArray{QStringLiteral("x"), QStringLiteral("a"), QStringLiteral("y")}, 3, &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("first_relevant_rank")).toInt() == 2);
    CHECK(result.value(QStringLiteral("reciprocal_rank")).toDouble() == Approx(0.5));
    CHECK(result.value(QStringLiteral("recall_at_k")).toDouble() == Approx(0.5));
    const QJsonArray missing = result.value(QStringLiteral("missing_relevant_ids")).toArray();
    REQUIRE(missing.size() == 1);
    CHECK(missing.at(0).toString() == QStringLiteral("z"));
}

TEST_CASE("retrieval evaluation fails closed on empty reference duplicate ids and invalid k", "[vibecut][retrieval-eval][integrity]")
{
    QString error;
    CHECK(evaluateVibeCutRetrievalRanking(QJsonArray{}, QJsonArray{}, 5, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("1..1000")));

    error.clear();
    CHECK(evaluateVibeCutRetrievalRanking(QJsonArray{QStringLiteral("a")},
                                          QJsonArray{QStringLiteral("x"), QStringLiteral("x")}, 2, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("unique"), Qt::CaseInsensitive));

    error.clear();
    CHECK(evaluateVibeCutRetrievalRanking(QJsonArray{QStringLiteral("a")}, QJsonArray{QStringLiteral("a")}, 0, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("1..1000")));
}

TEST_CASE("retrieval evaluation tool is read only reference based and exposes no quality threshold", "[vibecut][retrieval-eval][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("retrieval_ranking_evaluate")));
    CHECK(policies.value(QStringLiteral("retrieval_ranking_evaluate")).risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(policies.value(QStringLiteral("retrieval_ranking_evaluate")).mutatesProject);

    const QJsonObject schema = schemaByName(surface, QStringLiteral("retrieval_ranking_evaluate"));
    REQUIRE_FALSE(schema.isEmpty());
    const QJsonObject properties = schema.value(QStringLiteral("input_schema")).toObject()
                                       .value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("relevant_ids")));
    CHECK(properties.contains(QStringLiteral("ranked_ids")));
    CHECK(properties.contains(QStringLiteral("k")));
    CHECK_FALSE(properties.contains(QStringLiteral("pass_threshold")));
    CHECK_FALSE(properties.contains(QStringLiteral("quality_score")));
    CHECK_FALSE(properties.contains(QStringLiteral("auto_execute")));
}
