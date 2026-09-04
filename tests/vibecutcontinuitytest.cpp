/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutcontinuity.h"
#include "vibecut/vibecutroughcutsynthesis.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
VibeCutMediaDocument doc(const QString &id, int start, int end, const QString &text,
                         const QString &source = QString(), const QString &fingerprint = QString())
{
    VibeCutMediaDocument document;
    document.id = id;
    document.kind = QStringLiteral("transcript_segment");
    document.startFrame = start;
    document.endFrame = end;
    document.text = text;
    document.metadata = QJsonObject{{QStringLiteral("evidence_origin"), QStringLiteral("extractor")}};
    if (!source.isEmpty()) document.metadata.insert(QStringLiteral("source_id"), source);
    if (!fingerprint.isEmpty()) document.metadata.insert(QStringLiteral("source_fingerprint"), fingerprint);
    return document;
}

QJsonObject schemaByName(const VibeCutToolSurface &surface, const QString &name)
{
    for (const QJsonValue &value : surface.schemas()) {
        const QJsonObject schema = value.toObject();
        if (schema.value(QStringLiteral("name")).toString() == name) return schema;
    }
    return {};
}
}

TEST_CASE("rough-cut continuity compares frame structure only inside one proven domain", "[vibecut][continuity]")
{
    const QList<VibeCutMediaDocument> documents{
        doc(QStringLiteral("a"), 0, 20, QStringLiteral("Same statement"), QStringLiteral("source-1"), QStringLiteral("fp-1")),
        doc(QStringLiteral("c"), 15, 35, QStringLiteral("Same statement"), QStringLiteral("source-1"), QStringLiteral("fp-1")),
        doc(QStringLiteral("e"), 0, 20, QStringLiteral("Different source"), QStringLiteral("source-2"), QStringLiteral("fp-2")),
        doc(QStringLiteral("d"), 30, 60, QStringLiteral("Closing thought"), QStringLiteral("source-1"), QStringLiteral("fp-1")),
        doc(QStringLiteral("b"), 50, 70, QStringLiteral("Middle thought"), QStringLiteral("source-1"), QStringLiteral("fp-1")),
    };
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(documents, 9, 20, 600, &error);
    REQUIRE(error.isEmpty());
    const QJsonArray selected{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("e")};
    const QJsonObject result = analyzeVibeCutRoughCutContinuity(context, selected, 9, &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("authority")).toString() == QStringLiteral("derived_analysis"));
    CHECK(result.value(QStringLiteral("chronology_reversal_count")).toInt() == 1);
    CHECK(result.value(QStringLiteral("overlapping_range_count")).toInt() == 1);
    CHECK(result.value(QStringLiteral("repeated_transcript_content_count")).toInt() == 1);
    CHECK(result.value(QStringLiteral("source_provenance_change_count")).toInt() == 1);
    CHECK(result.value(QStringLiteral("frame_comparison_skipped_due_to_domain_count")).toInt() == 1);
    CHECK(result.value(QStringLiteral("frame_domain_semantics")).toString().contains(QStringLiteral("same_provenance_coordinate_domain")));
    CHECK_FALSE(result.value(QStringLiteral("normative_thresholds_applied")).toBool(true));
    CHECK_FALSE(result.value(QStringLiteral("quality_claim")).toBool(true));
    CHECK_FALSE(result.value(QStringLiteral("executable")).toBool(true));
    CHECK(result.value(QStringLiteral("mutation_authority")).toString() == QStringLiteral("none"));

    const QJsonArray gaps = result.value(QStringLiteral("ranked_positive_source_gap_candidates")).toArray();
    REQUIRE(gaps.size() == 1);
    CHECK(gaps.at(0).toObject().value(QStringLiteral("positive_source_gap_frames")).toInt() == 30);
    CHECK(gaps.at(0).toObject().value(QStringLiteral("rank")).toInt() == 1);
}

TEST_CASE("rough-cut continuity fails closed through canonical proposal validation", "[vibecut][continuity][integrity]")
{
    const QList<VibeCutMediaDocument> documents{
        doc(QStringLiteral("a"), 0, 20, QStringLiteral("One")),
        doc(QStringLiteral("b"), 20, 40, QStringLiteral("Two")),
    };
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(documents, 4, 20, 600, &error);
    REQUIRE(error.isEmpty());

    CHECK(analyzeVibeCutRoughCutContinuity(context, QJsonArray{QStringLiteral("invented")}, 4, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("unknown"), Qt::CaseInsensitive));

    error.clear();
    CHECK(analyzeVibeCutRoughCutContinuity(context, QJsonArray{QStringLiteral("a")}, 5, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("stale"), Qt::CaseInsensitive));
}

TEST_CASE("rough-cut continuity surface is read only and exposes no caller quality threshold or geometry", "[vibecut][continuity][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("rough_cut_continuity_analyze")));
    CHECK(policies.value(QStringLiteral("rough_cut_continuity_analyze")).risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(policies.value(QStringLiteral("rough_cut_continuity_analyze")).mutatesProject);

    const QJsonObject schema = schemaByName(surface, QStringLiteral("rough_cut_continuity_analyze"));
    REQUIRE_FALSE(schema.isEmpty());
    const QJsonObject properties = schema.value(QStringLiteral("input_schema")).toObject()
                                       .value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("selected_candidate_ids")));
    CHECK_FALSE(properties.contains(QStringLiteral("quality_threshold")));
    CHECK_FALSE(properties.contains(QStringLiteral("gap_threshold")));
    CHECK_FALSE(properties.contains(QStringLiteral("start_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("end_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("operations")));
}
