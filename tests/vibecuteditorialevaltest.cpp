/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecuteditorialeval.h"

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
