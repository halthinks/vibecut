/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutduplicatefusion.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
QJsonObject component(bool available, double score)
{
    return QJsonObject{{QStringLiteral("available"), available}, {QStringLiteral("score"), score}};
}

QJsonObject schemaByName(const VibeCutToolSurface &surface, const QString &name)
{
    for (const QJsonValue &value : surface.schemas()) {
        const QJsonObject schema = value.toObject();
        if (schema.value(QStringLiteral("name")).toString() == name) return schema;
    }
    return QJsonObject();
}
}

TEST_CASE("duplicate fusion renormalizes only across available evidence", "[vibecut][duplicates][fusion]")
{
    const QJsonObject result = fuseVibeCutDuplicateSignals(QJsonObject{
        {QStringLiteral("mpeg7"), component(true, 1.0)},
        {QStringLiteral("siglip_visual"), component(true, 0.8)},
        {QStringLiteral("duration"), component(false, 0.0)},
    });
    CHECK(result.value(QStringLiteral("authority")).toString() == QStringLiteral("derived_candidate"));
    CHECK(result.value(QStringLiteral("score_semantics")).toString() == QStringLiteral("weighted_available_evidence_similarity_not_probability"));
    CHECK(result.value(QStringLiteral("independent_signal_count")).toInt() == 2);
    CHECK(result.value(QStringLiteral("available_weight")).toDouble() == Approx(0.55));
    // (1.0*0.30 + 0.8*0.25) / 0.55
    CHECK(result.value(QStringLiteral("fusion_score")).toDouble() == Approx(0.9090909).epsilon(1e-6));
    CHECK(result.value(QStringLiteral("classification")).toString() == QStringLiteral("near_duplicate_candidate"));
}

TEST_CASE("strong duplicate classification requires multiple independent high signals", "[vibecut][duplicates][fusion][coverage]")
{
    QJsonObject sparse{
        {QStringLiteral("mpeg7"), component(true, 1.0)},
    };
    const QJsonObject sparseResult = fuseVibeCutDuplicateSignals(sparse);
    CHECK(sparseResult.value(QStringLiteral("fusion_score")).toDouble() == Approx(1.0));
    CHECK(sparseResult.value(QStringLiteral("classification")).toString() == QStringLiteral("insufficient_evidence"));

    QJsonObject rich{
        {QStringLiteral("mpeg7"), component(true, 1.0)},
        {QStringLiteral("siglip_visual"), component(true, 0.94)},
        {QStringLiteral("siglip_temporal_alignment"), component(true, 0.92)},
        {QStringLiteral("duration"), component(true, 0.98)},
    };
    const QJsonObject richResult = fuseVibeCutDuplicateSignals(rich);
    CHECK(richResult.value(QStringLiteral("independent_signal_count")).toInt() == 4);
    CHECK(richResult.value(QStringLiteral("fusion_score")).toDouble() >= 0.90);
    CHECK(richResult.value(QStringLiteral("classification")).toString() == QStringLiteral("strong_duplicate_candidate"));
}

TEST_CASE("invalid duplicate signals are excluded rather than clamped into evidence", "[vibecut][duplicates][fusion][integrity]")
{
    QJsonObject components{
        {QStringLiteral("mpeg7"), component(true, 2.0)},
        {QStringLiteral("siglip_visual"), component(true, 0.8)},
        {QStringLiteral("duration"), component(true, 0.8)},
    };
    const QJsonObject result = fuseVibeCutDuplicateSignals(components);
    const QJsonArray invalid = result.value(QStringLiteral("invalid_components")).toArray();
    REQUIRE(invalid.size() == 1);
    CHECK(invalid.at(0).toString() == QStringLiteral("mpeg7"));
    CHECK(result.value(QStringLiteral("independent_signal_count")).toInt() == 2);
    CHECK(result.value(QStringLiteral("fusion_score")).toDouble() == Approx(0.8));
}

TEST_CASE("duplicate fusion and bounded project discovery are read-only normal tool-surface capabilities", "[vibecut][duplicates][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("media_duplicate_fusion")));
    const VibeCutToolPolicy policy = policies.value(QStringLiteral("media_duplicate_fusion"));
    CHECK(policy.risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(policy.asynchronous);
    CHECK_FALSE(policy.mutatesProject);

    const QJsonObject schema = schemaByName(surface, QStringLiteral("media_duplicate_fusion"));
    REQUIRE_FALSE(schema.isEmpty());
    const QJsonObject properties = schema.value(QStringLiteral("input_schema")).toObject()
                                       .value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("first_bin_id")));
    CHECK(properties.contains(QStringLiteral("second_bin_id")));
    CHECK_FALSE(properties.contains(QStringLiteral("weights")));
    CHECK_FALSE(properties.contains(QStringLiteral("vector")));
    CHECK_FALSE(properties.contains(QStringLiteral("probability_threshold")));

    REQUIRE(policies.contains(QStringLiteral("media_duplicate_candidates")));
    const VibeCutToolPolicy discovery = policies.value(QStringLiteral("media_duplicate_candidates"));
    CHECK(discovery.risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(discovery.asynchronous);
    CHECK_FALSE(discovery.mutatesProject);
    const QJsonObject discoverySchema = schemaByName(surface, QStringLiteral("media_duplicate_candidates"));
    REQUIRE_FALSE(discoverySchema.isEmpty());
    const QJsonObject discoveryProps = discoverySchema.value(QStringLiteral("input_schema")).toObject()
                                              .value(QStringLiteral("properties")).toObject();
    CHECK(discoveryProps.contains(QStringLiteral("bin_ids")));
    CHECK(discoveryProps.contains(QStringLiteral("max_assets")));
    CHECK(discoveryProps.contains(QStringLiteral("max_pairs")));
    CHECK(discoveryProps.contains(QStringLiteral("min_signals")));
    CHECK(discoveryProps.contains(QStringLiteral("min_score")));
    CHECK_FALSE(discoveryProps.contains(QStringLiteral("run_missing_extractors")));
}
