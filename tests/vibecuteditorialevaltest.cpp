/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "tests_definitions.h"
#include "vibecut/vibecuteditorialeval.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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

TEST_CASE("editorial selection evaluation reports exact agreement without claiming quality", "[vibecut][editorial-eval]")
{
    QString error;
    const QJsonObject result = evaluateVibeCutEditorialSelection(
        QJsonArray{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
        QJsonArray{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("precision")).toDouble() == Approx(1.0));
    CHECK(result.value(QStringLiteral("recall")).toDouble() == Approx(1.0));
    CHECK(result.value(QStringLiteral("f1")).toDouble() == Approx(1.0));
    CHECK(result.value(QStringLiteral("exact_set_match")).toBool(false));
    CHECK(result.value(QStringLiteral("exact_order_match")).toBool(false));
    CHECK(result.value(QStringLiteral("common_order_pair_agreement")).toDouble() == Approx(1.0));
    CHECK_FALSE(result.value(QStringLiteral("quality_claim")).toBool(true));
    CHECK(result.value(QStringLiteral("evaluation_semantics")).toString().contains(QStringLiteral("not_editorial_quality")));
}

TEST_CASE("editorial selection evaluation separates set agreement from ordering", "[vibecut][editorial-eval][order]")
{
    QString error;
    const QJsonObject result = evaluateVibeCutEditorialSelection(
        QJsonArray{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
        QJsonArray{QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("c")}, &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("exact_set_match")).toBool(false));
    CHECK_FALSE(result.value(QStringLiteral("exact_order_match")).toBool(true));
    CHECK(result.value(QStringLiteral("common_order_pair_count")).toInt() == 3);
    CHECK(result.value(QStringLiteral("common_order_agreeing_pair_count")).toInt() == 2);
    CHECK(result.value(QStringLiteral("common_order_pair_agreement")).toDouble() == Approx(2.0 / 3.0).epsilon(1e-9));
}

TEST_CASE("editorial selection evaluation reports missed and unexpected candidates explicitly", "[vibecut][editorial-eval][coverage]")
{
    QString error;
    const QJsonObject missed = evaluateVibeCutEditorialSelection(
        QJsonArray{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
        QJsonArray{QStringLiteral("a"), QStringLiteral("c")}, &error);
    REQUIRE(error.isEmpty());
    CHECK(missed.value(QStringLiteral("precision")).toDouble() == Approx(1.0));
    CHECK(missed.value(QStringLiteral("recall")).toDouble() == Approx(2.0 / 3.0).epsilon(1e-9));
    CHECK(missed.value(QStringLiteral("f1")).toDouble() == Approx(0.8).epsilon(1e-9));
    const QJsonArray missedIds = missed.value(QStringLiteral("missed_candidate_ids")).toArray();
    REQUIRE(missedIds.size() == 1);
    CHECK(missedIds.at(0).toString() == QStringLiteral("b"));

    error.clear();
    const QJsonObject extra = evaluateVibeCutEditorialSelection(
        QJsonArray{QStringLiteral("a"), QStringLiteral("b")},
        QJsonArray{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("x")}, &error);
    REQUIRE(error.isEmpty());
    CHECK(extra.value(QStringLiteral("precision")).toDouble() == Approx(2.0 / 3.0).epsilon(1e-9));
    CHECK(extra.value(QStringLiteral("recall")).toDouble() == Approx(1.0));
    CHECK(extra.value(QStringLiteral("f1")).toDouble() == Approx(0.8).epsilon(1e-9));
    const QJsonArray unexpectedIds = extra.value(QStringLiteral("unexpected_candidate_ids")).toArray();
    REQUIRE(unexpectedIds.size() == 1);
    CHECK(unexpectedIds.at(0).toString() == QStringLiteral("x"));
}

TEST_CASE("golden editorial agreement fixtures reproduce declared metrics", "[vibecut][editorial-eval][golden]")
{
    QFile file(sourcesPath + QStringLiteral("/dataset/vibecut/editorial_selection_cases.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    REQUIRE(parseError.error == QJsonParseError::NoError);
    REQUIRE(document.isObject());
    const QJsonObject root = document.object();
    CHECK(root.value(QStringLiteral("schema_version")).toInt() == 1);
    CHECK(root.value(QStringLiteral("semantics")).toString().contains(QStringLiteral("not_editorial_quality")));
    const QJsonArray fixtures = root.value(QStringLiteral("fixtures")).toArray();
    REQUIRE(fixtures.size() >= 6);

    for (const QJsonValue &value : fixtures) {
        REQUIRE(value.isObject());
        const QJsonObject fixture = value.toObject();
        const QString fixtureId = fixture.value(QStringLiteral("id")).toString();
        INFO("fixture: " << fixtureId.toStdString());
        REQUIRE_FALSE(fixtureId.isEmpty());
        QString error;
        const QJsonObject result = evaluateVibeCutEditorialSelection(
            fixture.value(QStringLiteral("expected_candidate_ids")).toArray(),
            fixture.value(QStringLiteral("actual_candidate_ids")).toArray(), &error);
        REQUIRE(error.isEmpty());
        const QJsonObject expected = fixture.value(QStringLiteral("expected_metrics")).toObject();
        CHECK(result.value(QStringLiteral("precision")).toDouble() == Approx(expected.value(QStringLiteral("precision")).toDouble()).epsilon(1e-9));
        CHECK(result.value(QStringLiteral("recall")).toDouble() == Approx(expected.value(QStringLiteral("recall")).toDouble()).epsilon(1e-9));
        CHECK(result.value(QStringLiteral("f1")).toDouble() == Approx(expected.value(QStringLiteral("f1")).toDouble()).epsilon(1e-9));
        CHECK(result.value(QStringLiteral("exact_set_match")).toBool() == expected.value(QStringLiteral("exact_set_match")).toBool());
        CHECK(result.value(QStringLiteral("exact_order_match")).toBool() == expected.value(QStringLiteral("exact_order_match")).toBool());
        CHECK(result.value(QStringLiteral("common_order_pair_agreement")).toDouble() == Approx(expected.value(QStringLiteral("common_order_pair_agreement")).toDouble()).epsilon(1e-9));
        if (expected.contains(QStringLiteral("missed_candidate_ids"))) {
            CHECK(result.value(QStringLiteral("missed_candidate_ids")).toArray() == expected.value(QStringLiteral("missed_candidate_ids")).toArray());
        }
        if (expected.contains(QStringLiteral("unexpected_candidate_ids"))) {
            CHECK(result.value(QStringLiteral("unexpected_candidate_ids")).toArray() == expected.value(QStringLiteral("unexpected_candidate_ids")).toArray());
        }
        CHECK_FALSE(result.value(QStringLiteral("quality_claim")).toBool(true));
    }
}

TEST_CASE("editorial selection evaluation fails closed on duplicate or malformed ids", "[vibecut][editorial-eval][integrity]")
{
    QString error;
    CHECK(evaluateVibeCutEditorialSelection(QJsonArray{QStringLiteral("a"), QStringLiteral("a")},
                                            QJsonArray{QStringLiteral("a")}, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("unique"), Qt::CaseInsensitive));
    error.clear();
    CHECK(evaluateVibeCutEditorialSelection(QJsonArray{QStringLiteral("a")},
                                            QJsonArray{1}, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("strings"), Qt::CaseInsensitive));
}

TEST_CASE("editorial evaluation tool is read-only reference based and has no quality or edit controls", "[vibecut][editorial-eval][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("editorial_selection_evaluate")));
    const VibeCutToolPolicy policy = policies.value(QStringLiteral("editorial_selection_evaluate"));
    CHECK(policy.risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(policy.asynchronous);
    CHECK_FALSE(policy.mutatesProject);

    const QJsonObject schema = schemaByName(surface, QStringLiteral("editorial_selection_evaluate"));
    REQUIRE_FALSE(schema.isEmpty());
    const QJsonObject properties = schema.value(QStringLiteral("input_schema")).toObject()
                                       .value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("expected_candidate_ids")));
    CHECK(properties.contains(QStringLiteral("actual_candidate_ids")));
    CHECK_FALSE(properties.contains(QStringLiteral("weights")));
    CHECK_FALSE(properties.contains(QStringLiteral("quality_score")));
    CHECK_FALSE(properties.contains(QStringLiteral("start_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("end_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("operations")));
}
