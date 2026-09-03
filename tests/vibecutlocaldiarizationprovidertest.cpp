/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutextractorprovider.h"
#include "vibecut/vibecutlocaldiarizationprovider.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("built-in local pyannote provider is discoverable for diarization", "[vibecut][extractor-provider][diarization]")
{
    ensureVibeCutBuiltinExtractorProvidersRegistered();
    VibeCutExtractorProviderRegistry &registry = VibeCutExtractorProviderRegistry::global();
    CHECK(registry.providerIds().contains(QStringLiteral("local_pyannote")));
    CHECK(registry.providerIdsForCapability(QStringLiteral("diarization")).contains(QStringLiteral("local_pyannote")));

    QString error;
    std::unique_ptr<VibeCutExtractorProvider> provider = registry.create(QStringLiteral("local_pyannote"), &error);
    REQUIRE(provider);
    CHECK(error.isEmpty());
    CHECK(provider->id() == QStringLiteral("local_pyannote"));
    CHECK(provider->capabilities() == QStringList{QStringLiteral("diarization")});
    CHECK(provider->displayName().contains(QStringLiteral("pyannote"), Qt::CaseInsensitive));
}

TEST_CASE("diarization setup is always-confirm and chat schemas contain no credential input", "[vibecut][extractor-provider][diarization][policy]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const QHash<QString, VibeCutToolPolicy> policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("speaker_diarization_status")));
    REQUIRE(policies.contains(QStringLiteral("speaker_diarization_setup")));

    CHECK(policies.value(QStringLiteral("speaker_diarization_status")).risk == VibeCutToolRisk::ReadOnly);
    const VibeCutToolPolicy setup = policies.value(QStringLiteral("speaker_diarization_setup"));
    CHECK(setup.risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(setup.asynchronous);
    CHECK(setup.confirmationRequired);
    CHECK(setup.requiresConfirmation(VibeCutTrustMode::Turbo));
    CHECK_FALSE(setup.mutatesProject);

    for (const QJsonValue &value : surface.schemas()) {
        const QJsonObject schema = value.toObject();
        const QString name = schema.value(QStringLiteral("name")).toString();
        if (!name.startsWith(QStringLiteral("speaker_diarization"))) continue;
        const QJsonObject properties = schema.value(QStringLiteral("input_schema")).toObject().value(QStringLiteral("properties")).toObject();
        for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
            CHECK_FALSE(it.key().contains(QStringLiteral("token"), Qt::CaseInsensitive));
            CHECK_FALSE(it.key().contains(QStringLiteral("password"), Qt::CaseInsensitive));
            CHECK_FALSE(it.key().contains(QStringLiteral("secret"), Qt::CaseInsensitive));
        }
    }
}
