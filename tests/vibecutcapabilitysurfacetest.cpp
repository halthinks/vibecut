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
        QStringLiteral("bin_folder_create"),
        QStringLiteral("bin_import_file"),
        QStringLiteral("bin_insert_timeline"),
        QStringLiteral("bin_metadata_set"),
        QStringLiteral("bin_move_to_folder"),
        QStringLiteral("clip_move"),
        QStringLiteral("clip_split"),
        QStringLiteral("clip_trim"),
        QStringLiteral("composition_a_track_set"),
        QStringLiteral("effect_add"),
        QStringLiteral("effect_group_add"),
        QStringLiteral("effect_keyframe_add"),
        QStringLiteral("effect_keyframe_move"),
        QStringLiteral("effect_keyframe_remove"),
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
        QStringLiteral("transition_parameter_set"),
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
        QStringLiteral("bin_metadata_get"),
        QStringLiteral("bin_source_inspect"),
        QStringLiteral("bin_missing_list"),
        QStringLiteral("bin_folders_list"),
        QStringLiteral("bin_relink_scan_directory"),
        QStringLiteral("composition_a_track_inspect"),
        QStringLiteral("effect_group_inspect"),
        QStringLiteral("effect_keyframes_inspect"),
        QStringLiteral("effects_available"),
        QStringLiteral("effects_inspect"),
        QStringLiteral("mix_inspect"),
        QStringLiteral("project_preflight"),
        QStringLiteral("proxy_status"),
        QStringLiteral("render_recommend"),
        QStringLiteral("routing_status"),
        QStringLiteral("audio_target_set"),
        QStringLiteral("video_target_set"),
        QStringLiteral("selection_list"),
        QStringLiteral("selection_set"),
        QStringLiteral("selection_clear"),
        QStringLiteral("tracks_list"),
        QStringLiteral("transition_parameters_inspect"),
        QStringLiteral("transitions_list"),
        QStringLiteral("render_presets_list"),
    };
    for (const QString &name : readOnly) {
        INFO(name.toStdString());
        REQUIRE(policies.contains(name));
        CHECK(policies.value(name).risk == VibeCutToolRisk::ReadOnly);
        CHECK_FALSE(policies.value(name).mutatesProject);
    }

    const QStringList major = {
        QStringLiteral("bin_relink_missing"),
        QStringLiteral("bin_relink_missing_batch"),
        QStringLiteral("bin_replace_source"),
        QStringLiteral("clip_ripple_trim"),
        QStringLiteral("clip_delete"),
        QStringLiteral("track_delete"),
    };
    for (const QString &name : major) {
        INFO(name.toStdString());
        REQUIRE(policies.contains(name));
        CHECK(policies.value(name).risk == VibeCutToolRisk::MajorEdit);
        CHECK(policies.value(name).mutatesProject);
    }

    REQUIRE(policies.contains(QStringLiteral("proxy_set_enabled")));
    CHECK(policies.value(QStringLiteral("proxy_set_enabled")).risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(policies.value(QStringLiteral("proxy_set_enabled")).reversible);
    CHECK(policies.value(QStringLiteral("proxy_set_enabled")).mutatesProject);

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
