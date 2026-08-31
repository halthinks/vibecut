/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"

#include "vibecut/vibecutsubtitletools.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("subtitle search registers as a governed read-only extension", "[vibecut][subtitles]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    QString error;
    REQUIRE(registerVibeCutSubtitleTools(surface, &error));
    CHECK(error.isEmpty());

    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("subtitles_search")));
    CHECK(policies.value(QStringLiteral("subtitles_search")).risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(policies.value(QStringLiteral("subtitles_search")).mutatesProject);

    bool found = false;
    for (const QJsonValue &value : surface.schemas()) {
        if (value.toObject().value(QStringLiteral("name")).toString() == QLatin1String("subtitles_search")) {
            found = true;
            const QJsonObject input = value.toObject().value(QStringLiteral("input_schema")).toObject();
            CHECK(input.value(QStringLiteral("required")).toArray().contains(QStringLiteral("query")));
        }
    }
    CHECK(found);
}

TEST_CASE("subtitle search validates empty queries before touching live state", "[vibecut][subtitles]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    REQUIRE(registerVibeCutSubtitleTools(surface));

    const QJsonObject result = surface.invoke(QStringLiteral("subtitles_search"), QJsonObject{{QStringLiteral("query"), QStringLiteral("   ")}});
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("empty")));
}
