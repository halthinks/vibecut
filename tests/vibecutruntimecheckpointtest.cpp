/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutruntimecheckpoint.h"

TEST_CASE("runtime checkpoint groups consecutive synchronous mutations", "[vibecut][runtime-protocol][checkpoint]")
{
    int begins = 0;
    int ends = 0;
    int rollbacks = 0;
    QString label;
    VibeCutRuntimeCheckpoint checkpoint(
        [&begins, &label](const QString &value) {
            ++begins;
            label = value;
            return true;
        },
        [&ends]() {
            ++ends;
            return true;
        },
        [&rollbacks]() {
            ++rollbacks;
            return true;
        });

    QString error;
    REQUIRE(checkpoint.beginForMutation(QStringLiteral("assemble interview"), &error));
    REQUIRE(checkpoint.beginForMutation(QStringLiteral("assemble interview"), &error));
    CHECK(error.isEmpty());
    CHECK(checkpoint.macroOpen());
    CHECK(begins == 1);
    CHECK(label.contains(QStringLiteral("assemble interview")));

    REQUIRE(checkpoint.commitForCompletion(&error));
    CHECK_FALSE(checkpoint.macroOpen());
    CHECK(ends == 1);
    CHECK(rollbacks == 0);
    CHECK(checkpoint.committedCheckpointCount() == 1);
    CHECK(checkpoint.rolledBackCheckpointCount() == 0);
}

TEST_CASE("runtime checkpoint commits before async and opens a new later checkpoint", "[vibecut][runtime-protocol][checkpoint][async]")
{
    int begins = 0;
    int ends = 0;
    int rollbacks = 0;
    VibeCutRuntimeCheckpoint checkpoint(
        [&begins](const QString &) { ++begins; return true; },
        [&ends]() { ++ends; return true; },
        [&rollbacks]() { ++rollbacks; return true; });

    QString error;
    REQUIRE(checkpoint.beginForMutation(QStringLiteral("plan"), &error));
    REQUIRE(checkpoint.commitBeforeAsync(&error));
    CHECK_FALSE(checkpoint.macroOpen());
    CHECK(checkpoint.committedCheckpointCount() == 1);

    REQUIRE(checkpoint.beginForMutation(QStringLiteral("plan"), &error));
    REQUIRE(checkpoint.commitForCompletion(&error));
    CHECK(begins == 2);
    CHECK(ends == 2);
    CHECK(rollbacks == 0);
    CHECK(checkpoint.committedCheckpointCount() == 2);
}

TEST_CASE("runtime checkpoint rolls back only the currently open synchronous macro", "[vibecut][runtime-protocol][checkpoint][rollback]")
{
    int begins = 0;
    int ends = 0;
    int rollbacks = 0;
    VibeCutRuntimeCheckpoint checkpoint(
        [&begins](const QString &) { ++begins; return true; },
        [&ends]() { ++ends; return true; },
        [&rollbacks]() { ++rollbacks; return true; });

    QString error;
    REQUIRE(checkpoint.beginForMutation(QStringLiteral("plan"), &error));
    REQUIRE(checkpoint.commitBeforeAsync(&error));
    REQUIRE(checkpoint.beginForMutation(QStringLiteral("plan"), &error));
    REQUIRE(checkpoint.rollbackOpen(&error));

    CHECK_FALSE(checkpoint.macroOpen());
    CHECK(begins == 2);
    CHECK(ends == 1);
    CHECK(rollbacks == 1);
    CHECK(checkpoint.committedCheckpointCount() == 1);
    CHECK(checkpoint.rolledBackCheckpointCount() == 1);
    // The first checkpoint was already committed before async and is not
    // retroactively claimed as part of the rollback.
}

TEST_CASE("runtime checkpoint fails closed when editor checkpoint callbacks fail", "[vibecut][runtime-protocol][checkpoint][failure]")
{
    VibeCutRuntimeCheckpoint beginFailure(
        [](const QString &) { return false; },
        []() { return true; },
        []() { return true; });
    QString error;
    CHECK_FALSE(beginFailure.beginForMutation(QStringLiteral("plan"), &error));
    CHECK_FALSE(error.isEmpty());
    CHECK_FALSE(beginFailure.macroOpen());

    VibeCutRuntimeCheckpoint commitFailure(
        [](const QString &) { return true; },
        []() { return false; },
        []() { return true; });
    error.clear();
    REQUIRE(commitFailure.beginForMutation(QStringLiteral("plan"), &error));
    CHECK_FALSE(commitFailure.commitForCompletion(&error));
    CHECK(commitFailure.macroOpen());
    CHECK_FALSE(error.isEmpty());

    error.clear();
    REQUIRE(commitFailure.rollbackOpen(&error));
    CHECK_FALSE(commitFailure.macroOpen());
}
