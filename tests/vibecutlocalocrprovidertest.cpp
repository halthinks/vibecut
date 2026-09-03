/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutextractorprovider.h"
#include "vibecut/vibecutlocalocrprovider.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("built-in local OCR provider is discoverable without prior setup calls", "[vibecut][ocr][extractor-provider]")
{
    ensureVibeCutLocalOcrProviderRegistered();
    VibeCutExtractorProviderRegistry &registry = VibeCutExtractorProviderRegistry::global();
    CHECK(registry.providerIdsForCapability(QStringLiteral("ocr")).contains(QStringLiteral("local_tesseract")));

    QString error;
    std::unique_ptr<VibeCutExtractorProvider> provider = registry.create(QStringLiteral("local_tesseract"), &error);
    REQUIRE(provider);
    CHECK(error.isEmpty());
    CHECK(provider->capabilities() == QStringList{QStringLiteral("ocr")});
    CHECK(provider->displayName().contains(QStringLiteral("Tesseract"), Qt::CaseInsensitive));
}

TEST_CASE("first-class OCR tool is bounded async evidence work and exposes no source path", "[vibecut][ocr][policy]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("media_ocr_refresh")));
    const VibeCutToolPolicy policy = policies.value(QStringLiteral("media_ocr_refresh"));
    CHECK(policy.risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(policy.asynchronous);
    CHECK_FALSE(policy.mutatesProject);

    QJsonObject schema;
    for (const QJsonValue &value : surface.schemas()) {
        if (value.toObject().value(QStringLiteral("name")).toString() == QLatin1String("media_ocr_refresh")) {
            schema = value.toObject();
            break;
        }
    }
    REQUIRE_FALSE(schema.isEmpty());
    const QJsonObject input = schema.value(QStringLiteral("input_schema")).toObject();
    const QJsonObject properties = input.value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("bin_id")));
    CHECK(properties.contains(QStringLiteral("start_frame")));
    CHECK(properties.contains(QStringLiteral("end_frame")));
    CHECK(properties.contains(QStringLiteral("sample_interval_frames")));
    CHECK(properties.contains(QStringLiteral("max_samples")));
    CHECK(properties.contains(QStringLiteral("language")));
    CHECK(properties.contains(QStringLiteral("min_confidence")));
    CHECK_FALSE(properties.contains(QStringLiteral("source_path")));
    CHECK_FALSE(properties.contains(QStringLiteral("ffmpeg")));
    CHECK_FALSE(properties.contains(QStringLiteral("tesseract")));
}
