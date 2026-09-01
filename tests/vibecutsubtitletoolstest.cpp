/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"

#include "vibecut/vibecutsubtitletools.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("subtitle tools register with governance and scoped generation schema", "[vibecut][subtitles]")
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
    REQUIRE(policies.contains(QStringLiteral("generate_subtitles")));
    CHECK(policies.value(QStringLiteral("generate_subtitles")).risk == VibeCutToolRisk::MajorEdit);

    bool foundSearch = false;
    bool foundGeneration = false;
    for (const QJsonValue &value : surface.schemas()) {
        const QJsonObject schema = value.toObject();
        const QString name = schema.value(QStringLiteral("name")).toString();
        if (name == QLatin1String("subtitles_search")) {
            foundSearch = true;
            const QJsonObject input = schema.value(QStringLiteral("input_schema")).toObject();
            CHECK(input.value(QStringLiteral("required")).toArray().contains(QStringLiteral("query")));
        } else if (name == QLatin1String("generate_subtitles")) {
            foundGeneration = true;
            const QJsonObject properties = schema.value(QStringLiteral("input_schema")).toObject().value(QStringLiteral("properties")).toObject();
            REQUIRE(properties.contains(QStringLiteral("scope")));
            const QJsonArray scopes = properties.value(QStringLiteral("scope")).toObject().value(QStringLiteral("enum")).toArray();
            CHECK(scopes.contains(QStringLiteral("auto")));
            CHECK(scopes.contains(QStringLiteral("whole_project")));
        }
    }
    CHECK(foundSearch);
    CHECK(foundGeneration);
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

TEST_CASE("whole-project subtitle scope delegates explicitly to native validation", "[vibecut][subtitles]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    REQUIRE(registerVibeCutSubtitleTools(surface));

    // No timeline exists in this unit test. Explicit whole_project must bypass
    // auto-scope ambiguity and reach the native handler, which reports that
    // live-state error in the usual way.
    const QJsonObject result = surface.invoke(QStringLiteral("generate_subtitles"),
                                              QJsonObject{{QStringLiteral("scope"), QStringLiteral("whole_project")}});
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("timeline"), Qt::CaseInsensitive));
}
