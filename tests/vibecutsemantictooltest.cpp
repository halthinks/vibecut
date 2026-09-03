/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"

#include "vibecut/vibecutmediatools.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

TEST_CASE("semantic tools register beside deterministic lexical media search", "[vibecut][semantic][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    QString error;
    REQUIRE(registerVibeCutMediaTools(surface, &error));
    CHECK(error.isEmpty());

    const auto policies = surface.policies();
    for (const QString &name : {QStringLiteral("media_search"), QStringLiteral("semantic_status"),
                                QStringLiteral("semantic_setup"), QStringLiteral("semantic_text_refresh"),
                                QStringLiteral("semantic_search_text"), QStringLiteral("semantic_result")}) {
        INFO(name.toStdString());
        REQUIRE(policies.contains(name));
    }

    CHECK(policies.value(QStringLiteral("media_search")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("semantic_status")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("semantic_result")).risk == VibeCutToolRisk::ReadOnly);

    const VibeCutToolPolicy search = policies.value(QStringLiteral("semantic_search_text"));
    CHECK(search.risk == VibeCutToolRisk::ReadOnly);
    CHECK(search.asynchronous);
    CHECK_FALSE(search.mutatesProject);

    const VibeCutToolPolicy refresh = policies.value(QStringLiteral("semantic_text_refresh"));
    CHECK(refresh.risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(refresh.asynchronous);
    CHECK_FALSE(refresh.mutatesProject);

    const VibeCutToolPolicy setup = policies.value(QStringLiteral("semantic_setup"));
    CHECK(setup.risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(setup.asynchronous);
    CHECK(setup.confirmationRequired);
    CHECK(setup.requiresConfirmation(VibeCutTrustMode::Turbo));
    CHECK_FALSE(setup.mutatesProject);
}

TEST_CASE("semantic schemas expose bounded policy without model path or vector injection", "[vibecut][semantic][schema]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    QString error;
    REQUIRE(registerVibeCutMediaTools(surface, &error));

    QJsonObject refreshSchema;
    QJsonObject searchSchema;
    for (const QJsonValue &value : surface.schemas()) {
        const QJsonObject schema = value.toObject();
        const QString name = schema.value(QStringLiteral("name")).toString();
        if (name == QLatin1String("semantic_text_refresh")) refreshSchema = schema;
        if (name == QLatin1String("semantic_search_text")) searchSchema = schema;
    }
    REQUIRE_FALSE(refreshSchema.isEmpty());
    REQUIRE_FALSE(searchSchema.isEmpty());

    const QJsonObject refreshProperties = refreshSchema.value(QStringLiteral("input_schema")).toObject()
                                              .value(QStringLiteral("properties")).toObject();
    CHECK(refreshProperties.contains(QStringLiteral("device")));
    CHECK(refreshProperties.contains(QStringLiteral("batch_size")));
    CHECK_FALSE(refreshProperties.contains(QStringLiteral("model")));
    CHECK_FALSE(refreshProperties.contains(QStringLiteral("model_revision")));
    CHECK_FALSE(refreshProperties.contains(QStringLiteral("source_path")));
    CHECK_FALSE(refreshProperties.contains(QStringLiteral("vector")));

    const QJsonObject searchProperties = searchSchema.value(QStringLiteral("input_schema")).toObject()
                                             .value(QStringLiteral("properties")).toObject();
    CHECK(searchProperties.contains(QStringLiteral("query")));
    CHECK(searchProperties.contains(QStringLiteral("limit")));
    CHECK(searchProperties.contains(QStringLiteral("min_similarity")));
    CHECK(searchProperties.contains(QStringLiteral("device")));
    CHECK_FALSE(searchProperties.contains(QStringLiteral("model")));
    CHECK_FALSE(searchProperties.contains(QStringLiteral("embedding")));
    CHECK_FALSE(searchProperties.contains(QStringLiteral("vector")));
}
