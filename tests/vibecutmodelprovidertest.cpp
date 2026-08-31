/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutmodelprovider.h"

TEST_CASE("model provider registry exposes Anthropic without coupling the agent", "[vibecut][provider]")
{
    VibeCutModelProviderRegistry registry = VibeCutModelProviderRegistry::builtIns();
    CHECK(registry.providerIds().contains(QStringLiteral("anthropic")));
    QString error;
    std::unique_ptr<VibeCutModelProvider> provider = registry.create(QStringLiteral("anthropic"), &error);
    REQUIRE(provider);
    CHECK(provider->id() == QStringLiteral("anthropic"));
    const VibeCutModelRequest request = provider->buildRequest(QStringLiteral("system"), QJsonArray(), QJsonArray(), 1234);
    CHECK(request.endpoint.isValid());
    CHECK(request.body.value(QStringLiteral("max_tokens")).toInt() == 1234);
    CHECK(request.body.contains(QStringLiteral("tools")));
}

TEST_CASE("model provider registry rejects duplicate ids", "[vibecut][provider]")
{
    VibeCutModelProviderRegistry registry;
    QString error;
    REQUIRE(registry.registerProvider(QStringLiteral("fake"), []() {
        return std::unique_ptr<VibeCutModelProvider>(new VibeCutAnthropicProvider());
    }, &error));
    CHECK_FALSE(registry.registerProvider(QStringLiteral("fake"), []() {
        return std::unique_ptr<VibeCutModelProvider>(new VibeCutAnthropicProvider());
    }, &error));
}
