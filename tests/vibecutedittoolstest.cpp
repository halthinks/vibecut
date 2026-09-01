/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("core edit primitives register with governed risk levels", "[vibecut][edit-tools]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("clip_move")));
    REQUIRE(policies.contains(QStringLiteral("clip_trim")));
    REQUIRE(policies.contains(QStringLiteral("clip_ripple_trim")));
    REQUIRE(policies.contains(QStringLiteral("timeline_range_remove")));
    REQUIRE(policies.contains(QStringLiteral("clip_delete")));
    REQUIRE(policies.contains(QStringLiteral("repeated_take_selection_plan")));
    REQUIRE(policies.contains(QStringLiteral("repeated_take_selection_execute")));
    CHECK(policies.value(QStringLiteral("clip_move")).risk == VibeCutToolRisk::ReversibleEdit);
    CHECK(policies.value(QStringLiteral("clip_trim")).risk == VibeCutToolRisk::ReversibleEdit);
    CHECK(policies.value(QStringLiteral("clip_ripple_trim")).risk == VibeCutToolRisk::MajorEdit);
    CHECK(policies.value(QStringLiteral("timeline_range_remove")).risk == VibeCutToolRisk::MajorEdit);
    CHECK(policies.value(QStringLiteral("clip_delete")).risk == VibeCutToolRisk::MajorEdit);
    CHECK(policies.value(QStringLiteral("repeated_take_selection_plan")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("repeated_take_selection_execute")).risk == VibeCutToolRisk::MajorEdit);
    CHECK(policies.value(QStringLiteral("timeline_range_remove")).reversible);
    CHECK(policies.value(QStringLiteral("timeline_range_remove")).mutatesProject);
    CHECK(policies.value(QStringLiteral("repeated_take_selection_execute")).reversible);
    CHECK(policies.value(QStringLiteral("repeated_take_selection_execute")).mutatesProject);
}

TEST_CASE("edit tools reject invalid input before claiming mutation", "[vibecut][edit-tools]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const QJsonObject badMove = surface.invoke(QStringLiteral("clip_move"),
                                               QJsonObject{{QStringLiteral("clip_id"), -1}, {QStringLiteral("position_frame"), 10}});
    CHECK_FALSE(badMove.value(QStringLiteral("ok")).toBool());
    const QJsonObject badTrim = surface.invoke(QStringLiteral("clip_trim"),
                                               QJsonObject{{QStringLiteral("clip_id"), -1}, {QStringLiteral("side"), QStringLiteral("banana")},
                                                           {QStringLiteral("new_duration_frames"), 10}});
    CHECK_FALSE(badTrim.value(QStringLiteral("ok")).toBool());
    const QJsonObject badRange = surface.invoke(QStringLiteral("timeline_range_remove"),
                                                QJsonObject{{QStringLiteral("start_frame"), 20}, {QStringLiteral("end_frame"), 10},
                                                            {QStringLiteral("mode"), QStringLiteral("ripple")}});
    CHECK_FALSE(badRange.value(QStringLiteral("ok")).toBool());
    const QJsonObject badTakeExecution = surface.invoke(QStringLiteral("repeated_take_selection_execute"),
                                                        QJsonObject{{QStringLiteral("selections"), QJsonArray{}},
                                                                    {QStringLiteral("remove_mode"), QStringLiteral("ripple")}});
    CHECK_FALSE(badTakeExecution.value(QStringLiteral("ok")).toBool());
}
