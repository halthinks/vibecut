/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutextractorprovider.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("built-in AudioSet provider is discoverable before any setup call", "[vibecut][audio-events][provider]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);

    const QStringList providers = VibeCutExtractorProviderRegistry::global().providerIdsForCapability(QStringLiteral("audio_events"));
    REQUIRE(providers.contains(QStringLiteral("local_ast_audioset")));

    QString error;
    std::unique_ptr<VibeCutExtractorProvider> provider = VibeCutExtractorProviderRegistry::global().create(QStringLiteral("local_ast_audioset"), &error);
    REQUIRE(provider);
    CHECK(error.isEmpty());
    CHECK(provider->capabilities() == QStringList{QStringLiteral("audio_events")});
    CHECK(provider->displayName().contains(QStringLiteral("AudioSet")));
}

TEST_CASE("audio-event setup is hard-confirm while classification remains bounded async evidence work", "[vibecut][audio-events][policy]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();

    REQUIRE(policies.contains(QStringLiteral("audio_event_status")));
    CHECK(policies.value(QStringLiteral("audio_event_status")).risk == VibeCutToolRisk::ReadOnly);

    REQUIRE(policies.contains(QStringLiteral("audio_event_setup")));
    const VibeCutToolPolicy setup = policies.value(QStringLiteral("audio_event_setup"));
    CHECK(setup.risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(setup.asynchronous);
    CHECK(setup.confirmationRequired);
    CHECK(setup.requiresConfirmation(VibeCutTrustMode::Turbo));
    CHECK_FALSE(setup.mutatesProject);

    REQUIRE(policies.contains(QStringLiteral("media_audio_events_refresh")));
    const VibeCutToolPolicy classify = policies.value(QStringLiteral("media_audio_events_refresh"));
    CHECK(classify.risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(classify.asynchronous);
    CHECK_FALSE(classify.mutatesProject);
}

TEST_CASE("first-class audio-event schema exposes bounded policy not source or runtime injection", "[vibecut][audio-events][schema]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    QJsonObject target;
    for (const QJsonValue &value : surface.schemas()) {
        const QJsonObject schema = value.toObject();
        if (schema.value(QStringLiteral("name")).toString() == QLatin1String("media_audio_events_refresh")) {
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
}
