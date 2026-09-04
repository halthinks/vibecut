/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutnarrative.h"
#include "vibecut/vibecutroughcutsynthesis.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
VibeCutMediaDocument doc(const QString &id, int start, int end, const QString &text)
{
    VibeCutMediaDocument document;
    document.id = id;
    document.kind = QStringLiteral("transcript_segment");
    document.startFrame = start;
    document.endFrame = end;
    document.text = text;
    document.metadata = QJsonObject{{QStringLiteral("evidence_origin"), QStringLiteral("extractor")},
                                    {QStringLiteral("source_id"), QStringLiteral("timeline:abc")},
                                    {QStringLiteral("source_fingerprint"), QStringLiteral("fp-current")}};
    return document;
}

QJsonArray vectorAt(int index, double sign = 1.0)
{
    QJsonArray vector;
    for (int i = 0; i < 384; ++i) vector.append(i == index ? sign : 0.0);
    return vector;
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

TEST_CASE("narrative analysis ranks low adjacent continuity and high nonadjacent repetition relatively", "[vibecut][narrative]")
{
    QList<VibeCutMediaDocument> documents{
        doc(QStringLiteral("a"), 0, 30, QStringLiteral("engine launch sequence")),
        doc(QStringLiteral("b"), 40, 70, QStringLiteral("engine launch procedure")),
        doc(QStringLiteral("c"), 80, 110, QStringLiteral("budget finance costs")),
        doc(QStringLiteral("d"), 120, 150, QStringLiteral("engine launch sequence again")),
    };
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(documents, 11, 20, 600, &error);
    REQUIRE(error.isEmpty());
    QJsonObject semantic{{QStringLiteral("a"), vectorAt(0)},
                         {QStringLiteral("b"), vectorAt(0)},
                         {QStringLiteral("c"), vectorAt(0, -1.0)},
                         {QStringLiteral("d"), vectorAt(0)}};
    const QJsonObject result = analyzeVibeCutNarrativeSequence(
        context, QJsonArray{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")},
        semantic, 11, 2, 5, &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("authority")).toString() == QStringLiteral("derived_analysis"));
    CHECK_FALSE(result.value(QStringLiteral("normative_thresholds_applied")).toBool(true));
    CHECK(result.value(QStringLiteral("semantic_adjacency_coverage")).toDouble() == Approx(1.0));
    const QJsonArray boundaries = result.value(QStringLiteral("section_boundary_candidates")).toArray();
    REQUIRE(boundaries.size() == 2);
    CHECK(boundaries.at(0).toObject().value(QStringLiteral("continuity_score")).toDouble() == Approx(0.0));
    CHECK(boundaries.at(0).toObject().value(QStringLiteral("candidate_semantics")).toString().contains(QStringLiteral("not_fact")));

    const QJsonArray repetitions = result.value(QStringLiteral("repetition_candidates")).toArray();
    REQUIRE_FALSE(repetitions.isEmpty());
    CHECK(repetitions.at(0).toObject().value(QStringLiteral("first_candidate_id")).toString() == QStringLiteral("a"));
    CHECK(repetitions.at(0).toObject().value(QStringLiteral("second_candidate_id")).toString() == QStringLiteral("d"));
    CHECK(repetitions.at(0).toObject().value(QStringLiteral("repetition_similarity_score")).toDouble() == Approx(1.0));
    CHECK_FALSE(result.value(QStringLiteral("executable")).toBool(true));
    CHECK(result.value(QStringLiteral("mutation_authority")).toString() == QStringLiteral("none"));
}

TEST_CASE("narrative analysis falls back to lexical similarity when semantic vectors are absent", "[vibecut][narrative][lexical]")
{
    QList<VibeCutMediaDocument> documents{
        doc(QStringLiteral("a"), 0, 20, QStringLiteral("red engine housing")),
        doc(QStringLiteral("b"), 30, 50, QStringLiteral("red engine cover")),
        doc(QStringLiteral("c"), 60, 80, QStringLiteral("market demand forecast")),
    };
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(documents, 4, 20, 600, &error);
    REQUIRE(error.isEmpty());
    const QJsonObject result = analyzeVibeCutNarrativeSequence(
        context, QJsonArray{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
        QJsonObject{}, 4, 5, 5, &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("semantic_adjacency_coverage")).toDouble() == Approx(0.0));
    const QJsonArray edges = result.value(QStringLiteral("adjacency_continuity")).toArray();
    REQUIRE(edges.size() == 2);
    CHECK(edges.at(0).toObject().value(QStringLiteral("continuity_component_used")).toString() == QStringLiteral("lexical_jaccard"));
    CHECK(edges.at(0).toObject().value(QStringLiteral("continuity_score")).toDouble() >
          edges.at(1).toObject().value(QStringLiteral("continuity_score")).toDouble());
}

TEST_CASE("narrative analysis reuses canonical proposal validation", "[vibecut][narrative][integrity]")
{
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(
        QList<VibeCutMediaDocument>{doc(QStringLiteral("a"), 0, 20, QStringLiteral("one"))}, 2, 20, 600, &error);
    REQUIRE(error.isEmpty());
    CHECK(analyzeVibeCutNarrativeSequence(context, QJsonArray{QStringLiteral("invented")}, QJsonObject{}, 2, 5, 5, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("unknown"), Qt::CaseInsensitive));
    error.clear();
    CHECK(analyzeVibeCutNarrativeSequence(context, QJsonArray{QStringLiteral("a")}, QJsonObject{}, 3, 5, 5, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("stale"), Qt::CaseInsensitive));
}

TEST_CASE("narrative tool is read-only and exposes no narrative thresholds or edit geometry", "[vibecut][narrative][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("rough_cut_narrative_analyze")));
    CHECK(policies.value(QStringLiteral("rough_cut_narrative_analyze")).risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(policies.value(QStringLiteral("rough_cut_narrative_analyze")).mutatesProject);
    const QJsonObject schema = schemaByName(surface, QStringLiteral("rough_cut_narrative_analyze"));
    REQUIRE_FALSE(schema.isEmpty());
    const QJsonObject properties = schema.value(QStringLiteral("input_schema")).toObject().value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("selected_candidate_ids")));
    CHECK(properties.contains(QStringLiteral("max_boundary_candidates")));
    CHECK_FALSE(properties.contains(QStringLiteral("continuity_threshold")));
    CHECK_FALSE(properties.contains(QStringLiteral("quality_threshold")));
    CHECK_FALSE(properties.contains(QStringLiteral("start_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("end_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("weights")));
}

TEST_CASE("narrative direct invocation rejects fractional candidate limits before project lookup", "[vibecut][narrative][integrity]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const QJsonObject result = surface.invoke(QStringLiteral("rough_cut_narrative_analyze"),
        QJsonObject{{QStringLiteral("base_revision"), 0},
                    {QStringLiteral("context_sha256"), QString(64, QLatin1Char('a'))},
                    {QStringLiteral("context_max_candidates"), 200},
                    {QStringLiteral("context_max_text_chars"), 600},
                    {QStringLiteral("selected_candidate_ids"), QJsonArray{QStringLiteral("a")}},
                    {QStringLiteral("max_boundary_candidates"), 2.5}});
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool(true));
    CHECK(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("max_boundary_candidates")));
    CHECK(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("integer"), Qt::CaseInsensitive));
}
