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
        QStringLiteral("bin_import_file"),
        QStringLiteral("bin_insert_timeline"),
        QStringLiteral("clip_move"),
        QStringLiteral("clip_split"),
        QStringLiteral("clip_trim"),
        QStringLiteral("effect_add"),
        QStringLiteral("effect_remove"),
        QStringLiteral("effect_parameter_set"),
        QStringLiteral("group_create"),
        QStringLiteral("group_ungroup"),
        QStringLiteral("guide_add"),
        QStringLiteral("guide_range_add"),
        QStringLiteral("guide_remove"),
        QStringLiteral("mix_add_previous"),
        QStringLiteral("mix_resize"),
        QStringLiteral("mix_remove"),
        QStringLiteral("subtitle_edit"),
        QStringLiteral("subtitle_delete"),
        QStringLiteral("title_create"),
        QStringLiteral("title_update"),
        QStringLiteral("track_create"),
        QStringLiteral("track_rename"),
        QStringLiteral("track_move"),
        QStringLiteral("track_set_locked"),
        QStringLiteral("track_set_enabled"),
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

    const QStringList readOnly = {
        QStringLiteral("bin_list"),
        QStringLiteral("effects_available"),
        QStringLiteral("effects_inspect"),
        QStringLiteral("mix_inspect"),
        QStringLiteral("selection_list"),
        QStringLiteral("selection_set"),
        QStringLiteral("selection_clear"),
        QStringLiteral("tracks_list"),
        QStringLiteral("transitions_list"),
        QStringLiteral("render_presets_list"),
    };
    for (const QString &name : readOnly) {
        INFO(name.toStdString());
        REQUIRE(policies.contains(name));
        CHECK(policies.value(name).risk == VibeCutToolRisk::ReadOnly);
    }

    REQUIRE(policies.contains(QStringLiteral("clip_ripple_trim")));
    CHECK(policies.value(QStringLiteral("clip_ripple_trim")).risk == VibeCutToolRisk::MajorEdit);
    REQUIRE(policies.contains(QStringLiteral("clip_delete")));
    CHECK(policies.value(QStringLiteral("clip_delete")).risk == VibeCutToolRisk::MajorEdit);
    REQUIRE(policies.contains(QStringLiteral("track_delete")));
    CHECK(policies.value(QStringLiteral("track_delete")).risk == VibeCutToolRisk::MajorEdit);

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
