/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("canonical effect tools are registered and fail safely without a live clip", "[vibecut][effects]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();

    REQUIRE(policies.contains(QStringLiteral("effects_available")));
    REQUIRE(policies.contains(QStringLiteral("effects_inspect")));
    REQUIRE(policies.contains(QStringLiteral("effect_add")));
    REQUIRE(policies.contains(QStringLiteral("effect_remove")));
    REQUIRE(policies.contains(QStringLiteral("effect_parameter_set")));

    const QJsonObject available = surface.invoke(QStringLiteral("effects_available"), QJsonObject{});
    CHECK(available.value(QStringLiteral("ok")).toBool());
    CHECK(available.value(QStringLiteral("effects")).isArray());

    const QJsonObject inspect = surface.invoke(QStringLiteral("effects_inspect"),
                                               QJsonObject{{QStringLiteral("clip_id"), -1}});
    CHECK_FALSE(inspect.value(QStringLiteral("ok")).toBool());

    const QJsonObject addUnknown = surface.invoke(QStringLiteral("effect_add"),
                                                  QJsonObject{{QStringLiteral("clip_id"), -1},
                                                              {QStringLiteral("effect_id"), QStringLiteral("vibecut.effect.does.not.exist")}});
    CHECK_FALSE(addUnknown.value(QStringLiteral("ok")).toBool());

    const QJsonObject remove = surface.invoke(QStringLiteral("effect_remove"),
                                              QJsonObject{{QStringLiteral("clip_id"), -1},
                                                          {QStringLiteral("effect_id"), QStringLiteral("transform")}});
    CHECK_FALSE(remove.value(QStringLiteral("ok")).toBool());

    const QJsonObject setParam = surface.invoke(QStringLiteral("effect_parameter_set"),
                                                QJsonObject{{QStringLiteral("clip_id"), -1},
                                                            {QStringLiteral("row"), 0},
                                                            {QStringLiteral("parameter"), QStringLiteral("opacity")},
                                                            {QStringLiteral("value"), QStringLiteral("1")}});
    CHECK_FALSE(setParam.value(QStringLiteral("ok")).toBool());
}
