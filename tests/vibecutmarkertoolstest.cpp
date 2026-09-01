/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("canonical surface exposes governed timeline guide tools", "[vibecut][guides]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("guides_list")));
    REQUIRE(policies.contains(QStringLiteral("guide_add")));
    REQUIRE(policies.contains(QStringLiteral("guide_range_add")));
    REQUIRE(policies.contains(QStringLiteral("guide_remove")));
    CHECK(policies.value(QStringLiteral("guides_list")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("guide_add")).risk == VibeCutToolRisk::ReversibleEdit);
    CHECK(policies.value(QStringLiteral("guide_range_add")).reversible);
}

TEST_CASE("guide mutations reject invalid frames before live mutation", "[vibecut][guides]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const QJsonObject result = surface.invoke(QStringLiteral("guide_add"),
                                              QJsonObject{{QStringLiteral("frame"), -1}, {QStringLiteral("comment"), QStringLiteral("bad")}});
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
}
