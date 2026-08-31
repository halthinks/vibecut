/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"

#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("canonical VibeCut surface includes governed editing and delivery capabilities", "[vibecut][tools]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();

    const QStringList required = {
        QStringLiteral("clip_move"), QStringLiteral("clip_split"), QStringLiteral("clip_trim"), QStringLiteral("clip_ripple_trim"),
        QStringLiteral("clip_delete"), QStringLiteral("guides_list"), QStringLiteral("guide_add"), QStringLiteral("guide_range_add"),
        QStringLiteral("guide_remove"), QStringLiteral("subtitle_edit"), QStringLiteral("subtitle_delete"),
        QStringLiteral("title_create"), QStringLiteral("transitions_list"), QStringLiteral("transition_add"),
        QStringLiteral("render_presets_list"), QStringLiteral("render_start")};
    for (const QString &name : required) {
        INFO(name.toStdString());
        REQUIRE(policies.contains(name));
    }

    CHECK(policies.value(QStringLiteral("clip_move")).risk == VibeCutToolRisk::ReversibleEdit);
    CHECK(policies.value(QStringLiteral("clip_delete")).risk == VibeCutToolRisk::MajorEdit);
    CHECK(policies.value(QStringLiteral("transitions_list")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("render_presets_list")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("render_start")).risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(policies.value(QStringLiteral("render_start")).asynchronous);
}

TEST_CASE("tool surface composes isolated extensions with canonical tools", "[vibecut][tools]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const int builtInSurfaceSize = surface.schemas().size();

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
    CHECK(surface.schemas().size() == builtInSurfaceSize + 1);
    CHECK(surface.invoke(QStringLiteral("example_read"), QJsonObject{}).value(QStringLiteral("value")).toInt() == 7);

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
    CHECK_FALSE(surface.registerTool(newSchema, policy, [](const QJsonObject &) { return QJsonObject{}; }, &error));
}
