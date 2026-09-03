/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
QJsonObject schemaByName(const VibeCutToolSurface &surface, const QString &name)
{
    for (const QJsonValue &value : surface.schemas()) {
        const QJsonObject schema = value.toObject();
        if (schema.value(QStringLiteral("name")).toString() == name) return schema;
    }
    return QJsonObject();
}
}

TEST_CASE("semantic and cross-modal tools are exposed on the normal VibeCut tool surface", "[vibecut][semantic][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();

    const QStringList expected{
        QStringLiteral("semantic_status"),
        QStringLiteral("semantic_setup"),
        QStringLiteral("semantic_text_refresh"),
        QStringLiteral("semantic_search_text"),
        QStringLiteral("semantic_result"),
        QStringLiteral("semantic_crossmodal_status"),
        QStringLiteral("semantic_visual_refresh"),
        QStringLiteral("semantic_search_visual"),
        QStringLiteral("semantic_crossmodal_result"),
        QStringLiteral("media_search_hybrid"),
        QStringLiteral("media_search_hybrid_result"),
    };
    for (const QString &name : expected) {
        INFO(name.toStdString());
        CHECK(policies.contains(name));
        CHECK_FALSE(schemaByName(surface, name).isEmpty());
    }

    REQUIRE(policies.contains(QStringLiteral("semantic_setup")));
    const VibeCutToolPolicy setup = policies.value(QStringLiteral("semantic_setup"));
    CHECK(setup.risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(setup.asynchronous);
    CHECK(setup.confirmationRequired);
    CHECK(setup.requiresConfirmation(VibeCutTrustMode::Turbo));
    CHECK_FALSE(setup.mutatesProject);

    REQUIRE(policies.contains(QStringLiteral("semantic_text_refresh")));
    CHECK(policies.value(QStringLiteral("semantic_text_refresh")).asynchronous);
    CHECK_FALSE(policies.value(QStringLiteral("semantic_text_refresh")).mutatesProject);

    REQUIRE(policies.contains(QStringLiteral("semantic_search_text")));
    CHECK(policies.value(QStringLiteral("semantic_search_text")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("semantic_search_text")).asynchronous);

    REQUIRE(policies.contains(QStringLiteral("semantic_visual_refresh")));
    CHECK(policies.value(QStringLiteral("semantic_visual_refresh")).risk == VibeCutToolRisk::ExternalSideEffect);
    CHECK(policies.value(QStringLiteral("semantic_visual_refresh")).asynchronous);
    CHECK_FALSE(policies.value(QStringLiteral("semantic_visual_refresh")).mutatesProject);

    REQUIRE(policies.contains(QStringLiteral("semantic_search_visual")));
    CHECK(policies.value(QStringLiteral("semantic_search_visual")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("semantic_search_visual")).asynchronous);

    REQUIRE(policies.contains(QStringLiteral("media_search_hybrid")));
    CHECK(policies.value(QStringLiteral("media_search_hybrid")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("media_search_hybrid")).asynchronous);
    CHECK_FALSE(policies.value(QStringLiteral("media_search_hybrid")).mutatesProject);
}

TEST_CASE("semantic first-class schemas expose bounded intent rather than model or path injection", "[vibecut][semantic][schema]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);

    const QJsonObject textRefresh = schemaByName(surface, QStringLiteral("semantic_text_refresh"));
    REQUIRE_FALSE(textRefresh.isEmpty());
    const QJsonObject textProps = textRefresh.value(QStringLiteral("input_schema")).toObject()
                                      .value(QStringLiteral("properties")).toObject();
    CHECK(textProps.contains(QStringLiteral("device")));
    CHECK(textProps.contains(QStringLiteral("batch_size")));
    CHECK_FALSE(textProps.contains(QStringLiteral("model")));
    CHECK_FALSE(textProps.contains(QStringLiteral("model_revision")));
    CHECK_FALSE(textProps.contains(QStringLiteral("source_path")));

    const QJsonObject visualRefresh = schemaByName(surface, QStringLiteral("semantic_visual_refresh"));
    REQUIRE_FALSE(visualRefresh.isEmpty());
    const QJsonObject visualProps = visualRefresh.value(QStringLiteral("input_schema")).toObject()
                                        .value(QStringLiteral("properties")).toObject();
    CHECK(visualProps.contains(QStringLiteral("bin_id")));
    CHECK(visualProps.contains(QStringLiteral("sample_interval_frames")));
    CHECK(visualProps.contains(QStringLiteral("max_samples")));
    CHECK_FALSE(visualProps.contains(QStringLiteral("source_path")));
    CHECK_FALSE(visualProps.contains(QStringLiteral("ffmpeg")));
    CHECK_FALSE(visualProps.contains(QStringLiteral("model")));
    CHECK_FALSE(visualProps.contains(QStringLiteral("model_revision")));

    const QJsonObject visualSearch = schemaByName(surface, QStringLiteral("semantic_search_visual"));
    REQUIRE_FALSE(visualSearch.isEmpty());
    const QJsonObject searchProps = visualSearch.value(QStringLiteral("input_schema")).toObject()
                                        .value(QStringLiteral("properties")).toObject();
    CHECK(searchProps.contains(QStringLiteral("query")));
    CHECK(searchProps.contains(QStringLiteral("limit")));
    CHECK(searchProps.contains(QStringLiteral("min_similarity")));
    CHECK_FALSE(searchProps.contains(QStringLiteral("vector")));
    CHECK_FALSE(searchProps.contains(QStringLiteral("model")));

    const QJsonObject hybrid = schemaByName(surface, QStringLiteral("media_search_hybrid"));
    REQUIRE_FALSE(hybrid.isEmpty());
    const QJsonObject hybridProps = hybrid.value(QStringLiteral("input_schema")).toObject()
                                        .value(QStringLiteral("properties")).toObject();
    CHECK(hybridProps.contains(QStringLiteral("query")));
    CHECK(hybridProps.contains(QStringLiteral("limit")));
    CHECK(hybridProps.contains(QStringLiteral("min_score")));
    CHECK_FALSE(hybridProps.contains(QStringLiteral("weights")));
    CHECK_FALSE(hybridProps.contains(QStringLiteral("vector")));
    CHECK_FALSE(hybridProps.contains(QStringLiteral("model")));
}
