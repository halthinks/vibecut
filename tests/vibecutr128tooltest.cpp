/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("R128 audio profile is bounded async evidence work while room-tone inference is read-only", "[vibecut][audio][r128][policy]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();

    REQUIRE(policies.contains(QStringLiteral("media_audio_profile_refresh")));
    const VibeCutToolPolicy profile = policies.value(QStringLiteral("media_audio_profile_refresh"));
    CHECK(profile.risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(profile.asynchronous);
    CHECK_FALSE(profile.mutatesProject);

    REQUIRE(policies.contains(QStringLiteral("media_room_tone_candidates")));
    const VibeCutToolPolicy roomTone = policies.value(QStringLiteral("media_room_tone_candidates"));
    CHECK(roomTone.risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(roomTone.asynchronous);
    CHECK_FALSE(roomTone.mutatesProject);

    QJsonObject profileSchema;
    for (const QJsonValue &value : surface.schemas()) {
        const QJsonObject schema = value.toObject();
        if (schema.value(QStringLiteral("name")).toString() == QLatin1String("media_audio_profile_refresh")) {
            profileSchema = schema;
            break;
        }
    }
    REQUIRE_FALSE(profileSchema.isEmpty());
    const QJsonObject properties = profileSchema.value(QStringLiteral("input_schema")).toObject()
                                       .value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("bin_id")));
    CHECK(properties.contains(QStringLiteral("start_frame")));
    CHECK(properties.contains(QStringLiteral("end_frame")));
    CHECK(properties.contains(QStringLiteral("sample_interval_ms")));
    CHECK(properties.contains(QStringLiteral("max_samples")));
    CHECK_FALSE(properties.contains(QStringLiteral("source_path")));
    CHECK_FALSE(properties.contains(QStringLiteral("ffmpeg")));
}
