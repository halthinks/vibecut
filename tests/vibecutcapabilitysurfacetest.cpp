/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("canonical VibeCut surface exposes governed editing breadth", "[vibecut][capabilities]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();

    const QStringList reversible = {
        QStringLiteral("clip_move"),
        QStringLiteral("clip_split"),
        QStringLiteral("clip_trim"),
        QStringLiteral("effect_add"),
        QStringLiteral("effect_remove"),
        QStringLiteral("effect_parameter_set"),
        QStringLiteral("guide_add"),
        QStringLiteral("guide_range_add"),
        QStringLiteral("guide_remove"),
        QStringLiteral("subtitle_edit"),
        QStringLiteral("subtitle_delete"),
        QStringLiteral("title_create"),
        QStringLiteral("transition_add"),
        QStringLiteral("transition_move"),
        QStringLiteral("transition_resize"),
        QStringLiteral("transition_remove"),
    };
    for (const QString &name : reversible) {
        INFO(name.toStdString());
        REQUIRE(policies.contains(name));
        CHECK(policies.value(name).reversible);
        CHECK(policies.value(name).mutatesProject);
        CHECK(policies.value(name).risk == VibeCutToolRisk::ReversibleEdit);
    }

    REQUIRE(policies.contains(QStringLiteral("effects_available")));
    CHECK(policies.value(QStringLiteral("effects_available")).risk == VibeCutToolRisk::ReadOnly);
    REQUIRE(policies.contains(QStringLiteral("effects_inspect")));
    CHECK(policies.value(QStringLiteral("effects_inspect")).risk == VibeCutToolRisk::ReadOnly);

    REQUIRE(policies.contains(QStringLiteral("clip_ripple_trim")));
    CHECK(policies.value(QStringLiteral("clip_ripple_trim")).risk == VibeCutToolRisk::MajorEdit);
    REQUIRE(policies.contains(QStringLiteral("clip_delete")));
    CHECK(policies.value(QStringLiteral("clip_delete")).risk == VibeCutToolRisk::MajorEdit);

    REQUIRE(policies.contains(QStringLiteral("transitions_list")));
    CHECK(policies.value(QStringLiteral("transitions_list")).risk == VibeCutToolRisk::ReadOnly);
    REQUIRE(policies.contains(QStringLiteral("render_presets_list")));
    CHECK(policies.value(QStringLiteral("render_presets_list")).risk == VibeCutToolRisk::ReadOnly);
    REQUIRE(policies.contains(QStringLiteral("render_start")));
    CHECK(policies.value(QStringLiteral("render_start")).risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(policies.value(QStringLiteral("render_start")).asynchronous);
}

TEST_CASE("canonical schemas have one unique policy each", "[vibecut][capabilities]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    QSet<QString> names;
    for (const QJsonValue &value : surface.schemas()) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        REQUIRE_FALSE(name.isEmpty());
        CHECK_FALSE(names.contains(name));
        names.insert(name);
        CHECK(policies.contains(name));
    }
    CHECK(names.size() == policies.size());
}
