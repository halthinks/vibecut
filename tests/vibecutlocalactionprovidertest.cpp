/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutextractorprovider.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("built-in X-CLIP action provider is discoverable before setup", "[vibecut][actions][provider]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const QStringList providers = VibeCutExtractorProviderRegistry::global().providerIdsForCapability(QStringLiteral("actions"));
    REQUIRE(providers.contains(QStringLiteral("local_xclip_actions")));

    QString error;
    std::unique_ptr<VibeCutExtractorProvider> provider = VibeCutExtractorProviderRegistry::global().create(QStringLiteral("local_xclip_actions"), &error);
    REQUIRE(provider);
    CHECK(error.isEmpty());
    CHECK(provider->capabilities() == QStringList{QStringLiteral("actions")});
    CHECK(provider->displayName().contains(QStringLiteral("X-CLIP")));
}

TEST_CASE("X-CLIP analysis reuses hard-confirm vision setup but predictions do not mutate project", "[vibecut][actions][policy]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();

    REQUIRE(policies.contains(QStringLiteral("vision_setup")));
    const VibeCutToolPolicy setup = policies.value(QStringLiteral("vision_setup"));
    CHECK(setup.risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(setup.asynchronous);
    CHECK(setup.confirmationRequired);
    CHECK(setup.requiresConfirmation(VibeCutTrustMode::Turbo));

    REQUIRE(policies.contains(QStringLiteral("media_actions_refresh")));
    const VibeCutToolPolicy action = policies.value(QStringLiteral("media_actions_refresh"));
    CHECK(action.risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(action.asynchronous);
    CHECK_FALSE(action.mutatesProject);
}

TEST_CASE("first-class action schema exposes bounded cadence not model vocabulary or paths", "[vibecut][actions][schema]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    QJsonObject target;
    for (const QJsonValue &value : surface.schemas()) {
        const QJsonObject schema = value.toObject();
        if (schema.value(QStringLiteral("name")).toString() == QLatin1String("media_actions_refresh")) {
            target = schema;
            break;
        }
    }
    REQUIRE_FALSE(target.isEmpty());
    const QJsonObject properties = target.value(QStringLiteral("input_schema")).toObject()
                                       .value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("bin_id")));
    CHECK(properties.contains(QStringLiteral("start_frame")));
    CHECK(properties.contains(QStringLiteral("end_frame")));
    CHECK(properties.contains(QStringLiteral("window_seconds")));
    CHECK(properties.contains(QStringLiteral("hop_seconds")));
    CHECK(properties.contains(QStringLiteral("max_windows")));
    CHECK(properties.contains(QStringLiteral("top_k")));
    CHECK(properties.contains(QStringLiteral("min_score")));
    CHECK(properties.contains(QStringLiteral("device")));
    CHECK_FALSE(properties.contains(QStringLiteral("source_path")));
    CHECK_FALSE(properties.contains(QStringLiteral("ffmpeg")));
    CHECK_FALSE(properties.contains(QStringLiteral("provider_id")));
    CHECK_FALSE(properties.contains(QStringLiteral("model")));
    CHECK_FALSE(properties.contains(QStringLiteral("revision")));
    CHECK_FALSE(properties.contains(QStringLiteral("labels")));
    CHECK_FALSE(properties.contains(QStringLiteral("prompts")));
}
