/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutedittools.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("core edit primitives register with governed risk levels", "[vibecut][edit-tools]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    QString error;
    REQUIRE(registerVibeCutEditTools(surface, &error));
    CHECK(error.isEmpty());
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("clip_move")));
    REQUIRE(policies.contains(QStringLiteral("clip_trim")));
    REQUIRE(policies.contains(QStringLiteral("clip_ripple_trim")));
    REQUIRE(policies.contains(QStringLiteral("clip_delete")));
    CHECK(policies.value(QStringLiteral("clip_move")).risk == VibeCutToolRisk::ReversibleEdit);
    CHECK(policies.value(QStringLiteral("clip_trim")).risk == VibeCutToolRisk::ReversibleEdit);
    CHECK(policies.value(QStringLiteral("clip_ripple_trim")).risk == VibeCutToolRisk::MajorEdit);
    CHECK(policies.value(QStringLiteral("clip_delete")).risk == VibeCutToolRisk::MajorEdit);
    CHECK(policies.value(QStringLiteral("clip_delete")).reversible);
}

TEST_CASE("edit tools reject invalid input before claiming mutation", "[vibecut][edit-tools]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    REQUIRE(registerVibeCutEditTools(surface));
    const QJsonObject badMove = surface.invoke(QStringLiteral("clip_move"),
                                               QJsonObject{{QStringLiteral("clip_id"), -1}, {QStringLiteral("position_frame"), 10}});
    CHECK_FALSE(badMove.value(QStringLiteral("ok")).toBool());
    const QJsonObject badTrim = surface.invoke(QStringLiteral("clip_trim"),
                                               QJsonObject{{QStringLiteral("clip_id"), -1}, {QStringLiteral("side"), QStringLiteral("banana")},
                                                           {QStringLiteral("new_duration_frames"), 10}});
    CHECK_FALSE(badTrim.value(QStringLiteral("ok")).toBool());
}
