/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutpacing.h"
#include "vibecut/vibecutroughcutsynthesis.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
VibeCutMediaEvidenceRecord evidence(const QString &kind, int start, int end,
                                    const QString &source = QStringLiteral("bin:1"),
                                    const QString &fingerprint = QStringLiteral("fp-a"))
{
    VibeCutMediaEvidenceRecord record;
    record.id = kind + QStringLiteral(":%1:%2").arg(start).arg(end);
    record.sourceId = source;
    record.sourceFingerprint = fingerprint;
    record.extractorId = QStringLiteral("test");
    record.extractorVersion = QStringLiteral("1");
    record.kind = kind;
    record.startFrame = start;
    record.endFrame = end;
    record.confidence = -1.0;
    return record;
}

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
                                    {QStringLiteral("source_fingerprint"), QStringLiteral("fp-timeline")}};
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

TEST_CASE("source pacing merges silence and reports descriptive shot transcript and speaker metrics", "[vibecut][pacing][source]")
{
    QList<VibeCutMediaEvidenceRecord> records{
        evidence(QStringLiteral("shot_segment"), 0, 50),
        evidence(QStringLiteral("shot_segment"), 50, 100),
        evidence(QStringLiteral("shot_segment"), 100, 200),
        evidence(QStringLiteral("silence"), 40, 60),
        evidence(QStringLiteral("silence"), 55, 70),
        evidence(QStringLiteral("transcript_segment"), 0, 40),
        evidence(QStringLiteral("transcript_segment"), 80, 120),
    };
    VibeCutMediaEvidenceRecord speakerA1 = evidence(QStringLiteral("speaker_segment"), 0, 40);
    speakerA1.metadata.insert(QStringLiteral("speaker_cluster_id"), QStringLiteral("A"));
    records.append(speakerA1);
    VibeCutMediaEvidenceRecord speakerA2 = evidence(QStringLiteral("speaker_segment"), 40, 80);
    speakerA2.metadata.insert(QStringLiteral("speaker_cluster_id"), QStringLiteral("A"));
    records.append(speakerA2);
    VibeCutMediaEvidenceRecord speakerB = evidence(QStringLiteral("speaker_segment"), 80, 120);
    speakerB.metadata.insert(QStringLiteral("speaker_cluster_id"), QStringLiteral("B"));
    records.append(speakerB);

    QString error;
    const QJsonObject result = analyzeVibeCutSourcePacing(records, QStringLiteral("bin:1"), QStringLiteral("fp-a"), &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("authority")).toString() == QStringLiteral("derived_analysis"));
    CHECK_FALSE(result.value(QStringLiteral("normative_thresholds_applied")).toBool(true));
    CHECK(result.value(QStringLiteral("observed_extent_frames")).toInt() == 200);

    const QJsonObject shots = result.value(QStringLiteral("shot_duration_metrics")).toObject();
    CHECK(shots.value(QStringLiteral("count")).toInt() == 3);
    CHECK(shots.value(QStringLiteral("mean_frames")).toDouble() == Approx(200.0 / 3.0).epsilon(1e-9));
    CHECK(shots.value(QStringLiteral("median_frames")).toDouble() == Approx(50.0));

    const QJsonObject silence = result.value(QStringLiteral("silence_metrics")).toObject();
    CHECK(silence.value(QStringLiteral("raw_range_count")).toInt() == 2);
    CHECK(silence.value(QStringLiteral("merged_range_count")).toInt() == 1);
    CHECK(silence.value(QStringLiteral("merged_silence_frames")).toInt() == 30);
    CHECK(silence.value(QStringLiteral("coverage_of_observed_extent")).toDouble() == Approx(0.15));

    const QJsonObject gaps = result.value(QStringLiteral("transcript_positive_gap_metrics")).toObject();
    CHECK(gaps.value(QStringLiteral("count")).toInt() == 1);
    CHECK(gaps.value(QStringLiteral("mean_frames")).toDouble() == Approx(40.0));

    const QJsonObject speakers = result.value(QStringLiteral("speaker_metrics")).toObject();
    CHECK(speakers.value(QStringLiteral("cluster_count")).toInt() == 2);
    CHECK(speakers.value(QStringLiteral("dominant_cluster_id")).toString() == QStringLiteral("A"));
    CHECK(speakers.value(QStringLiteral("dominant_share_of_raw_speaker_frames")).toDouble() == Approx(2.0 / 3.0).epsilon(1e-9));
}

TEST_CASE("source pacing filters exact source fingerprint rather than mixing stale evidence", "[vibecut][pacing][source][freshness]")
{
    QList<VibeCutMediaEvidenceRecord> records{
        evidence(QStringLiteral("shot_segment"), 0, 100, QStringLiteral("bin:1"), QStringLiteral("current")),
        evidence(QStringLiteral("shot_segment"), 0, 10, QStringLiteral("bin:1"), QStringLiteral("stale")),
    };
    QString error;
    const QJsonObject result = analyzeVibeCutSourcePacing(records, QStringLiteral("bin:1"), QStringLiteral("current"), &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("relevant_record_count")).toInt() == 1);
    CHECK(result.value(QStringLiteral("shot_duration_metrics")).toObject().value(QStringLiteral("mean_frames")).toDouble() == Approx(100.0));
}

TEST_CASE("rough-cut pacing reports duration rhythm and chronology without edit authority", "[vibecut][pacing][rough-cut]")
{
    QList<VibeCutMediaDocument> documents{
        doc(QStringLiteral("a"), 0, 30, QStringLiteral("Short")),
        doc(QStringLiteral("b"), 40, 100, QStringLiteral("Medium answer")),
        doc(QStringLiteral("c"), 120, 210, QStringLiteral("Long detailed answer")),
    };
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(documents, 6, 20, 600, &error);
    REQUIRE(error.isEmpty());
    const QJsonObject result = analyzeVibeCutRoughCutPacing(context,
        QJsonArray{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, 6, &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("authority")).toString() == QStringLiteral("derived_analysis"));
    CHECK(result.value(QStringLiteral("segment_count")).toInt() == 3);
    CHECK(result.value(QStringLiteral("total_frames")).toInt() == 180);
    const QJsonObject durations = result.value(QStringLiteral("duration_metrics")).toObject();
    CHECK(durations.value(QStringLiteral("mean_frames")).toDouble() == Approx(60.0));
    CHECK(durations.value(QStringLiteral("median_frames")).toDouble() == Approx(60.0));
    CHECK(durations.value(QStringLiteral("coefficient_of_variation")).toDouble() == Approx(0.40824829).epsilon(1e-6));
    const QJsonObject deltas = result.value(QStringLiteral("consecutive_duration_delta_metrics")).toObject();
    CHECK(deltas.value(QStringLiteral("mean_frames")).toDouble() == Approx(30.0));
    CHECK_FALSE(result.value(QStringLiteral("reorders_source_chronology")).toBool(true));
    CHECK_FALSE(result.value(QStringLiteral("normative_thresholds_applied")).toBool(true));
    CHECK_FALSE(result.value(QStringLiteral("executable")).toBool(true));
    CHECK(result.value(QStringLiteral("mutation_authority")).toString() == QStringLiteral("none"));

    error.clear();
    const QJsonObject reordered = analyzeVibeCutRoughCutPacing(context,
        QJsonArray{QStringLiteral("c"), QStringLiteral("a")}, 6, &error);
    REQUIRE(error.isEmpty());
    CHECK(reordered.value(QStringLiteral("reorders_source_chronology")).toBool(false));
}

TEST_CASE("pacing tools are read-only and rough-cut pacing exposes no raw geometry input", "[vibecut][pacing][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("media_source_pacing")));
    REQUIRE(policies.contains(QStringLiteral("rough_cut_pacing_analyze")));
    CHECK(policies.value(QStringLiteral("media_source_pacing")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("rough_cut_pacing_analyze")).risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(policies.value(QStringLiteral("rough_cut_pacing_analyze")).mutatesProject);

    const QJsonObject schema = schemaByName(surface, QStringLiteral("rough_cut_pacing_analyze"));
    REQUIRE_FALSE(schema.isEmpty());
    const QJsonObject properties = schema.value(QStringLiteral("input_schema")).toObject()
                                       .value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("selected_candidate_ids")));
    CHECK_FALSE(properties.contains(QStringLiteral("start_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("end_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("weights")));
    CHECK_FALSE(properties.contains(QStringLiteral("quality_threshold")));
}

TEST_CASE("rough-cut pacing direct invocation rejects fractional revision before project lookup", "[vibecut][pacing][integrity]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const QJsonObject result = surface.invoke(QStringLiteral("rough_cut_pacing_analyze"),
        QJsonObject{{QStringLiteral("base_revision"), 0.25},
                    {QStringLiteral("context_sha256"), QString(64, QLatin1Char('a'))},
                    {QStringLiteral("context_max_candidates"), 200},
                    {QStringLiteral("context_max_text_chars"), 600},
                    {QStringLiteral("selected_candidate_ids"), QJsonArray{QStringLiteral("a")}}});
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool(true));
    CHECK(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("base_revision")));
    CHECK(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("integer"), Qt::CaseInsensitive));
}
