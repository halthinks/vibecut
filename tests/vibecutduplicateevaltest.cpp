/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "tests_definitions.h"
#include "vibecut/vibecutduplicateeval.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QJsonObject pair(const QString &a, const QString &b,
                 double score = -1.0,
                 const QString &classification = QString(),
                 int signals = -1,
                 double availableWeight = -1.0)
{
    QJsonObject result{{QStringLiteral("first_bin_id"), a},
                       {QStringLiteral("second_bin_id"), b}};
    if (score >= 0.0) result.insert(QStringLiteral("fusion_score"), score);
    if (!classification.isEmpty()) result.insert(QStringLiteral("classification"), classification);
    if (signals >= 0) result.insert(QStringLiteral("independent_signal_count"), signals);
    if (availableWeight >= 0.0) result.insert(QStringLiteral("available_weight"), availableWeight);
    return result;
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

TEST_CASE("duplicate pair identity is order independent and refuses self pairs", "[vibecut][duplicate-eval][identity]")
{
    QString error;
    const QString ab = vibeCutDuplicatePairId(QStringLiteral("asset-a"), QStringLiteral("asset-b"), &error);
    REQUIRE(error.isEmpty());
    REQUIRE_FALSE(ab.isEmpty());
    const QString ba = vibeCutDuplicatePairId(QStringLiteral("asset-b"), QStringLiteral("asset-a"), &error);
    REQUIRE(error.isEmpty());
    CHECK(ab == ba);
    CHECK(ab.startsWith(QStringLiteral("pair:")));

    error.clear();
    CHECK(vibeCutDuplicatePairId(QStringLiteral("asset-a"), QStringLiteral("asset-a"), &error).isEmpty());
    CHECK(error.contains(QStringLiteral("distinct"), Qt::CaseInsensitive));
}

TEST_CASE("duplicate ranking reuses retrieval metrics while preserving fusion diagnostics", "[vibecut][duplicate-eval]")
{
    const QJsonArray relevant{
        pair(QStringLiteral("a"), QStringLiteral("b")),
        pair(QStringLiteral("c"), QStringLiteral("d")),
    };
    const QJsonArray ranked{
        pair(QStringLiteral("b"), QStringLiteral("a"), 0.96, QStringLiteral("strong_duplicate_candidate"), 4, 0.9),
        pair(QStringLiteral("x"), QStringLiteral("y"), 0.61, QStringLiteral("possible_related_candidate"), 2, 0.5),
        pair(QStringLiteral("d"), QStringLiteral("c"), 0.82, QStringLiteral("near_duplicate_candidate"), 3, 0.7),
    };
    QString error;
    const QJsonObject result = evaluateVibeCutDuplicateRanking(relevant, ranked, 3, &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("precision_at_k")).toDouble() == Approx(2.0 / 3.0).epsilon(1e-9));
    CHECK(result.value(QStringLiteral("recall_at_k")).toDouble() == Approx(1.0));
    CHECK(result.value(QStringLiteral("first_relevant_rank")).toInt() == 1);
    CHECK(result.value(QStringLiteral("classification_metadata_available_at_k")).toInt() == 3);
    CHECK(result.value(QStringLiteral("insufficient_evidence_count_at_k")).toInt() == 0);
    CHECK(result.value(QStringLiteral("mean_independent_signal_count_at_k")).toDouble() == Approx(3.0));
    CHECK(result.value(QStringLiteral("mean_available_weight_at_k")).toDouble() == Approx(0.7));
    CHECK_FALSE(result.value(QStringLiteral("duplicate_truth_claim")).toBool(true));
    CHECK(result.value(QStringLiteral("evaluation_semantics")).toString().contains(QStringLiteral("not_duplicate_truth")));
}

TEST_CASE("duplicate ranking refuses repeated unordered pairs and malformed fusion metadata", "[vibecut][duplicate-eval][integrity]")
{
    QString error;
    CHECK(evaluateVibeCutDuplicateRanking(
              QJsonArray{pair(QStringLiteral("a"), QStringLiteral("b"))},
              QJsonArray{pair(QStringLiteral("a"), QStringLiteral("b")), pair(QStringLiteral("b"), QStringLiteral("a"))},
              2, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("unordered"), Qt::CaseInsensitive));

    error.clear();
    CHECK(evaluateVibeCutDuplicateRanking(
              QJsonArray{pair(QStringLiteral("a"), QStringLiteral("b"))},
              QJsonArray{pair(QStringLiteral("a"), QStringLiteral("b"), 0.8, QStringLiteral("made_up_duplicate_fact"))},
              1, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("Unknown"), Qt::CaseInsensitive));

    error.clear();
    CHECK(evaluateVibeCutDuplicateRanking(
              QJsonArray{pair(QStringLiteral("a"), QStringLiteral("a"))}, QJsonArray{}, 1, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("distinct"), Qt::CaseInsensitive));
}

TEST_CASE("golden duplicate ranking metric corpus remains deterministic", "[vibecut][duplicate-eval][golden]")
{
    QFile file(sourcesPath + QStringLiteral("/dataset/vibecut/duplicate_ranking_cases.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    REQUIRE(parseError.error == QJsonParseError::NoError);
    REQUIRE(document.isObject());
    const QJsonObject root = document.object();
    CHECK(root.value(QStringLiteral("schema_version")).toInt() == 1);
    const QJsonArray fixtures = root.value(QStringLiteral("fixtures")).toArray();
    REQUIRE(fixtures.size() >= 2);
    for (const QJsonValue &value : fixtures) {
        REQUIRE(value.isObject());
        const QJsonObject fixture = value.toObject();
        INFO("fixture: " << fixture.value(QStringLiteral("id")).toString().toStdString());
        QString error;
        const QJsonObject result = evaluateVibeCutDuplicateRanking(
            fixture.value(QStringLiteral("relevant_pairs")).toArray(),
            fixture.value(QStringLiteral("ranked_pairs")).toArray(),
            fixture.value(QStringLiteral("k")).toInt(), &error);
        REQUIRE(error.isEmpty());
        const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
        CHECK(result.value(QStringLiteral("precision_at_k")).toDouble() == Approx(expected.value(QStringLiteral("precision_at_k")).toDouble()).epsilon(1e-9));
        CHECK(result.value(QStringLiteral("recall_at_k")).toDouble() == Approx(expected.value(QStringLiteral("recall_at_k")).toDouble()).epsilon(1e-9));
        CHECK(result.value(QStringLiteral("first_relevant_rank")).toInt() == expected.value(QStringLiteral("first_relevant_rank")).toInt());
        CHECK(result.value(QStringLiteral("insufficient_evidence_count_at_k")).toInt() == expected.value(QStringLiteral("insufficient_evidence_count_at_k")).toInt());
    }
}

TEST_CASE("duplicate ranking evaluation surface is read only and has no truth threshold", "[vibecut][duplicate-eval][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("duplicate_ranking_evaluate")));
    CHECK(policies.value(QStringLiteral("duplicate_ranking_evaluate")).risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(policies.value(QStringLiteral("duplicate_ranking_evaluate")).mutatesProject);

    const QJsonObject schema = schemaByName(surface, QStringLiteral("duplicate_ranking_evaluate"));
    REQUIRE_FALSE(schema.isEmpty());
    const QJsonObject properties = schema.value(QStringLiteral("input_schema")).toObject()
                                       .value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("relevant_pairs")));
    CHECK(properties.contains(QStringLiteral("ranked_pairs")));
    CHECK(properties.contains(QStringLiteral("k")));
    CHECK_FALSE(properties.contains(QStringLiteral("truth_threshold")));
    CHECK_FALSE(properties.contains(QStringLiteral("auto_merge")));
    CHECK_FALSE(properties.contains(QStringLiteral("delete_duplicates")));
}
