/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"

#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("tool surface composes isolated extensions with native tools", "[vibecut][tools]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);

    const QJsonObject schema{
        {QStringLiteral("name"), QStringLiteral("example_read")},
        {QStringLiteral("description"), QStringLiteral("test")},
        {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                     {QStringLiteral("properties"), QJsonObject{}}}},
    };
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("example_read");
    policy.risk = VibeCutToolRisk::ReadOnly;

    QString error;
    REQUIRE(surface.registerTool(schema, policy,
                                 [](const QJsonObject &) {
                                     return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("value"), 7}};
                                 },
                                 &error));
    CHECK(error.isEmpty());

    const QJsonArray schemas = surface.schemas();
    CHECK(schemas.size() == base.schemas().size() + 1);
    CHECK(surface.policies().contains(QStringLiteral("example_read")));
    CHECK(surface.invoke(QStringLiteral("example_read"), QJsonObject{}).value(QStringLiteral("value")).toInt() == 7);

    // Existing handlers still dispatch through the original implementation.
    const QJsonObject selection = surface.invoke(QStringLiteral("timeline_get_selection"), QJsonObject{});
    CHECK(selection.contains(QStringLiteral("ok")));
}

TEST_CASE("tool surface rejects duplicate and ungoverned registrations", "[vibecut][tools]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);

    const QJsonObject schema{
        {QStringLiteral("name"), QStringLiteral("timeline_list_clips")},
        {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
    };
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("timeline_list_clips");

    QString error;
    CHECK_FALSE(surface.registerTool(schema, policy, [](const QJsonObject &) { return QJsonObject{}; }, &error));
    CHECK_FALSE(error.isEmpty());

    QJsonObject newSchema = schema;
    newSchema.insert(QStringLiteral("name"), QStringLiteral("new_tool"));
    // Policy still names the old tool, so this must fail closed.
    CHECK_FALSE(surface.registerTool(newSchema, policy, [](const QJsonObject &) { return QJsonObject{}; }, &error));
}
