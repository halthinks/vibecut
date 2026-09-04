/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecuteditorialreview.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
QJsonObject review(const QString &reviewer,
                   int relevance,
                   int coherence,
                   int pacing,
                   int fidelity,
                   int preference,
                   const QString &candidate = QStringLiteral("candidate-a"),
                   const QString &caseId = QStringLiteral("case-1"),
                   bool blind = true)
{
    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("rubric_id"), vibeCutEditorialReviewRubricId()},
                       {QStringLiteral("blind"), blind},
                       {QStringLiteral("case_id"), caseId},
                       {QStringLiteral("candidate_id"), candidate},
                       {QStringLiteral("reviewer_id"), reviewer},
                       {QStringLiteral("task_type"), QStringLiteral("rough_cut")},
                       {QStringLiteral("scores"), QJsonObject{
                           {QStringLiteral("objective_relevance"), relevance},
                           {QStringLiteral("narrative_coherence"), coherence},
                           {QStringLiteral("pacing_fit"), pacing},
                           {QStringLiteral("source_fidelity"), fidelity},
                           {QStringLiteral("overall_preference"), preference}}}};
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

TEST_CASE("blinded editorial review validates fixed rubric without granting quality ground truth", "[vibecut][editorial-review]")
{
    QString error;
    const QJsonObject normalized = validateVibeCutEditorialReview(review(QStringLiteral("r1"), 5, 4, 3, 5, 4), &error);
    REQUIRE(error.isEmpty());
    CHECK(normalized.value(QStringLiteral("rubric_id")).toString() == vibeCutEditorialReviewRubricId());
    CHECK(normalized.value(QStringLiteral("authority")).toString() == QStringLiteral("human_review"));
    CHECK(normalized.value(QStringLiteral("blind")).toBool(false));
    CHECK_FALSE(normalized.value(QStringLiteral("quality_ground_truth")).toBool(true));
    CHECK(normalized.value(QStringLiteral("mutation_authority")).toString() == QStringLiteral("none"));
}

TEST_CASE("editorial review aggregation reports mean and disagreement without pass fail gate", "[vibecut][editorial-review][aggregate]")
{
    QJsonArray reviews{
        review(QStringLiteral("r1"), 5, 4, 3, 5, 4),
        review(QStringLiteral("r2"), 3, 4, 5, 5, 2),
        review(QStringLiteral("r3"), 4, 4, 4, 5, 3),
    };
    QString error;
    const QJsonObject result = aggregateVibeCutEditorialReviews(reviews, &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("authority")).toString() == QStringLiteral("human_review_aggregate"));
    CHECK(result.value(QStringLiteral("review_count")).toInt() == 3);
    CHECK_FALSE(result.value(QStringLiteral("automatic_execution_gate")).toBool(true));
    CHECK_FALSE(result.value(QStringLiteral("quality_ground_truth")).toBool(true));
    const QJsonObject metrics = result.value(QStringLiteral("metrics")).toObject();
    CHECK(metrics.value(QStringLiteral("objective_relevance")).toObject().value(QStringLiteral("mean")).toDouble() == Approx(4.0));
    CHECK(metrics.value(QStringLiteral("objective_relevance")).toObject().value(QStringLiteral("stddev")).toDouble() == Approx(std::sqrt(2.0 / 3.0)).epsilon(1e-9));
    CHECK(metrics.value(QStringLiteral("narrative_coherence")).toObject().value(QStringLiteral("stddev")).toDouble() == Approx(0.0));
    CHECK(metrics.value(QStringLiteral("source_fidelity")).toObject().value(QStringLiteral("mean")).toDouble() == Approx(5.0));
}

TEST_CASE("editorial review aggregation fails closed on duplicate reviewers mixed targets and non blind reviews", "[vibecut][editorial-review][integrity]")
{
    QString error;
    CHECK(aggregateVibeCutEditorialReviews(QJsonArray{
        review(QStringLiteral("r1"), 4, 4, 4, 4, 4),
        review(QStringLiteral("r1"), 5, 5, 5, 5, 5)}, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("Duplicate reviewer"), Qt::CaseInsensitive));

    error.clear();
    CHECK(aggregateVibeCutEditorialReviews(QJsonArray{
        review(QStringLiteral("r1"), 4, 4, 4, 4, 4),
        review(QStringLiteral("r2"), 4, 4, 4, 4, 4, QStringLiteral("candidate-b"))}, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("same case_id"), Qt::CaseInsensitive));

    error.clear();
    CHECK(validateVibeCutEditorialReview(review(QStringLiteral("r1"), 4, 4, 4, 4, 4,
                                                QStringLiteral("candidate-a"), QStringLiteral("case-1"), false), &error).isEmpty());
    CHECK(error.contains(QStringLiteral("blind=true"), Qt::CaseInsensitive));
}

TEST_CASE("editorial review rejects unknown criteria and out of range scores", "[vibecut][editorial-review][rubric]")
{
    QString error;
    QJsonObject badScore = review(QStringLiteral("r1"), 6, 4, 4, 4, 4);
    CHECK(validateVibeCutEditorialReview(badScore, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("1..5")));

    error.clear();
    QJsonObject extraCriterion = review(QStringLiteral("r1"), 4, 4, 4, 4, 4);
    QJsonObject scores = extraCriterion.value(QStringLiteral("scores")).toObject();
    scores.insert(QStringLiteral("made_up_quality"), 5);
    extraCriterion.insert(QStringLiteral("scores"), scores);
    CHECK(validateVibeCutEditorialReview(extraCriterion, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("Unknown"), Qt::CaseInsensitive));
}

TEST_CASE("editorial review tools are read only and expose no execution threshold", "[vibecut][editorial-review][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("editorial_review_validate")));
    REQUIRE(policies.contains(QStringLiteral("editorial_review_aggregate")));
    CHECK(policies.value(QStringLiteral("editorial_review_validate")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("editorial_review_aggregate")).risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(policies.value(QStringLiteral("editorial_review_aggregate")).mutatesProject);

    const QJsonObject schema = schemaByName(surface, QStringLiteral("editorial_review_aggregate"));
    REQUIRE_FALSE(schema.isEmpty());
    const QJsonObject properties = schema.value(QStringLiteral("input_schema")).toObject()
                                       .value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("reviews")));
    CHECK_FALSE(properties.contains(QStringLiteral("pass_threshold")));
    CHECK_FALSE(properties.contains(QStringLiteral("auto_execute")));
    CHECK_FALSE(properties.contains(QStringLiteral("quality_threshold")));
    CHECK_FALSE(properties.contains(QStringLiteral("operations")));
}
