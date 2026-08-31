/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"

#include "vibecut/vibecuttools.h"

#include <QSet>

TEST_CASE("every exposed vibecut tool has governance metadata", "[vibecut][policy]")
{
    VibeCutTools tools;
    const QJsonArray schemas = tools.schemas();
    const QHash<QString, VibeCutToolPolicy> policies = tools.policies();

    QSet<QString> schemaNames;
    for (const QJsonValue &value : schemas) {
        schemaNames.insert(value.toObject().value(QStringLiteral("name")).toString());
    }

    CHECK(schemaNames.size() == policies.size());
    for (const QString &name : schemaNames) {
        INFO(name.toStdString());
        REQUIRE(policies.contains(name));
        CHECK(policies.value(name).name == name);
    }
}

TEST_CASE("vibecut tool policies classify current side effects", "[vibecut][policy]")
{
    VibeCutTools tools;
    const auto policies = tools.policies();

    CHECK(policies.value(QStringLiteral("timeline_list_clips")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("effect_apply")).risk == VibeCutToolRisk::ReversibleEdit);
    CHECK(policies.value(QStringLiteral("effect_apply")).mutatesProject);
    CHECK(policies.value(QStringLiteral("speech_setup")).risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(policies.value(QStringLiteral("speech_setup")).asynchronous);
    CHECK(policies.value(QStringLiteral("generate_subtitles")).risk == VibeCutToolRisk::MajorEdit);
    CHECK(policies.value(QStringLiteral("generate_subtitles")).mutatesProject);
}
