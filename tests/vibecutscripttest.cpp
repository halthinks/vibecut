/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutscript.h"

#include <QJsonArray>
#include <QJsonObject>

TEST_CASE("VibeScript returns only a JSON plan object", "[vibecut][script]")
{
    const QString source = QStringLiteral(
        "({objective:'Mark a section', operations:[{id:'m1',tool:'guide_add',input:{frame:42,comment:'review'}}]})");
    const VibeCutScriptSandbox::Result result = VibeCutScriptSandbox::evaluatePlan(source, 100);
    REQUIRE(result.ok);
    CHECK_FALSE(result.timedOut);
    CHECK(result.value.value(QStringLiteral("objective")).toString() == QStringLiteral("Mark a section"));
    CHECK(result.value.value(QStringLiteral("operations")).toArray().size() == 1);
}

TEST_CASE("VibeScript rejects non-plan results", "[vibecut][script]")
{
    const VibeCutScriptSandbox::Result result = VibeCutScriptSandbox::evaluatePlan(QStringLiteral("21 + 21"), 100);
    CHECK_FALSE(result.ok);
    CHECK(result.error.contains(QStringLiteral("JSON object")));
}

TEST_CASE("VibeScript watchdog interrupts infinite loops", "[vibecut][script]")
{
    const VibeCutScriptSandbox::Result result = VibeCutScriptSandbox::evaluatePlan(QStringLiteral("while (true) {}"), 25);
    CHECK_FALSE(result.ok);
    CHECK(result.timedOut);
}

TEST_CASE("VibeScript receives no Node or Kdenlive host API", "[vibecut][script]")
{
    const QString source = QStringLiteral(
        "({objective:'sandbox check', operations:[{id:'x',tool:'guide_add',input:{frame:1,comment:(typeof process)+'/'+(typeof require)+'/'+(typeof kdenlive)}}]})");
    const VibeCutScriptSandbox::Result result = VibeCutScriptSandbox::evaluatePlan(source, 100);
    REQUIRE(result.ok);
    const QString comment = result.value.value(QStringLiteral("operations")).toArray().first().toObject()
                                .value(QStringLiteral("input")).toObject().value(QStringLiteral("comment")).toString();
    CHECK(comment == QStringLiteral("undefined/undefined/undefined"));
}
