/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecuthooks.h"

TEST_CASE("VibeCut hooks provide deterministic named context seams", "[vibecut][hooks]")
{
    VibeCutHooks hooks;
    QString error;
    REQUIRE(hooks.registerContextProvider(QStringLiteral("example"), []() {
        return QJsonObject{{QStringLiteral("ready"), true}};
    }, &error));
    CHECK(error.isEmpty());
    CHECK_FALSE(hooks.registerContextProvider(QStringLiteral("example"), []() { return QJsonObject(); }, &error));
    const QJsonObject context = hooks.collectContext();
    CHECK(context.value(QStringLiteral("example")).toObject().value(QStringLiteral("ready")).toBool());
    CHECK(hooks.unregisterContextProvider(QStringLiteral("example")));
}
