/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutextractorprovider.h"

class DummyExtractorProvider : public VibeCutExtractorProvider
{
public:
    QString id() const override { return QStringLiteral("dummy_test_provider"); }
    QString displayName() const override { return QStringLiteral("Dummy Test Provider"); }
    QStringList capabilities() const override { return {QStringLiteral("ocr"), QStringLiteral("embeddings")}; }
    bool configured(QString *error) const override
    {
        if (error) error->clear();
        return true;
    }
    QJsonObject start(const QString &capability, const QJsonObject &, VibeCutJobManager *, QString *error) override
    {
        if (error) error->clear();
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("capability"), capability}};
    }
};

TEST_CASE("extractor provider registry rejects invalid and duplicate registrations", "[vibecut][extractor-provider]")
{
    VibeCutExtractorProviderRegistry registry;
    QString error;
    CHECK_FALSE(registry.registerProvider(QString(), []() { return std::make_unique<DummyExtractorProvider>(); }, &error));
    CHECK_FALSE(error.isEmpty());

    error.clear();
    REQUIRE(registry.registerProvider(QStringLiteral("dummy"), []() { return std::make_unique<DummyExtractorProvider>(); }, &error));
    CHECK(error.isEmpty());
    CHECK_FALSE(registry.registerProvider(QStringLiteral("dummy"), []() { return std::make_unique<DummyExtractorProvider>(); }, &error));
    CHECK_FALSE(error.isEmpty());
}

TEST_CASE("extractor provider registry supports capability discovery and creation", "[vibecut][extractor-provider]")
{
    VibeCutExtractorProviderRegistry registry;
    QString error;
    REQUIRE(registry.registerProvider(QStringLiteral("dummy"), []() { return std::make_unique<DummyExtractorProvider>(); }, &error));

    CHECK(registry.providerIds() == QStringList{QStringLiteral("dummy")});
    CHECK(registry.providerIdsForCapability(QStringLiteral("OCR")) == QStringList{QStringLiteral("dummy")});
    CHECK(registry.providerIdsForCapability(QStringLiteral("diarization")).isEmpty());

    std::unique_ptr<VibeCutExtractorProvider> provider = registry.create(QStringLiteral("dummy"), &error);
    REQUIRE(provider);
    CHECK(provider->displayName() == QStringLiteral("Dummy Test Provider"));
    CHECK(provider->capabilities().contains(QStringLiteral("ocr")));

    error.clear();
    CHECK_FALSE(registry.create(QStringLiteral("missing"), &error));
    CHECK_FALSE(error.isEmpty());
}
