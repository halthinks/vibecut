/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutextractorprovider.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("built-in DETR provider is discoverable before any setup call", "[vibecut][objects][provider]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);

    const QStringList providers = VibeCutExtractorProviderRegistry::global().providerIdsForCapability(QStringLiteral("objects"));
    REQUIRE(providers.contains(QStringLiteral("local_detr_coco")));

    QString error;
    std::unique_ptr<VibeCutExtractorProvider> provider = VibeCutExtractorProviderRegistry::global().create(QStringLiteral("local_detr_coco"), &error);
    REQUIRE(provider);
    CHECK(error.isEmpty());
    CHECK(provider->capabilities() == QStringList{QStringLiteral("objects")});
    CHECK(provider->displayName().contains(QStringLiteral("DETR")));
}

TEST_CASE("vision setup is hard-confirm while object analysis remains bounded async evidence work", "[vibecut][objects][policy]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();

    REQUIRE(policies.contains(QStringLiteral("vision_status")));
    CHECK(policies.value(QStringLiteral("vision_status")).risk == VibeCutToolRisk::ReadOnly);

    REQUIRE(policies.contains(QStringLiteral("vision_setup")));
    const VibeCutToolPolicy setup = policies.value(QStringLiteral("vision_setup"));
    CHECK(setup.risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(setup.asynchronous);
    CHECK(setup.confirmationRequired);
    CHECK(setup.requiresConfirmation(VibeCutTrustMode::Turbo));
    CHECK_FALSE(setup.mutatesProject);

    REQUIRE(policies.contains(QStringLiteral("media_objects_refresh")));
    const VibeCutToolPolicy detect = policies.value(QStringLiteral("media_objects_refresh"));
    CHECK(detect.risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(detect.asynchronous);
    CHECK_FALSE(detect.mutatesProject);

    REQUIRE(policies.contains(QStringLiteral("media_object_tracks")));
    CHECK(policies.value(QStringLiteral("media_object_tracks")).risk == VibeCutToolRisk::ReadOnly);
}

TEST_CASE("first-class object schema exposes bounded policy not source model or runtime injection", "[vibecut][objects][schema]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    QJsonObject target;
    for (const QJsonValue &value : surface.schemas()) {
        const QJsonObject schema = value.toObject();
        if (schema.value(QStringLiteral("name")).toString() == QLatin1String("media_objects_refresh")) {
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
    CHECK(properties.contains(QStringLiteral("sample_interval_frames")));
    CHECK(properties.contains(QStringLiteral("max_samples")));
    CHECK(properties.contains(QStringLiteral("max_detections_per_frame")));
    CHECK(properties.contains(QStringLiteral("min_score")));
    CHECK(properties.contains(QStringLiteral("device")));
    CHECK_FALSE(properties.contains(QStringLiteral("source_path")));
    CHECK_FALSE(properties.contains(QStringLiteral("ffmpeg")));
    CHECK_FALSE(properties.contains(QStringLiteral("provider_id")));
    CHECK_FALSE(properties.contains(QStringLiteral("model")));
    CHECK_FALSE(properties.contains(QStringLiteral("revision")));
}
