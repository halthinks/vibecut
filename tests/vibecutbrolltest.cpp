/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutbroll.h"
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
                                    {QStringLiteral("source_fingerprint"), QStringLiteral("fp-transcript")},
                                    {QStringLiteral("extractor_id"), QStringLiteral("whisper_transcript")},
                                    {QStringLiteral("extractor_version"), QStringLiteral("model:turbo")}};
    return document;
}

QJsonObject searchResult(const QJsonObject &opportunity, quint64 revision)
{
    return QJsonObject{{QStringLiteral("kind"), QStringLiteral("broll_visual_candidates")},
                       {QStringLiteral("authority"), QStringLiteral("derived_ranking")},
                       {QStringLiteral("score_semantics"), QStringLiteral("current_siglip_visual_similarity_not_probability")},
                       {QStringLiteral("base_revision"), static_cast<qint64>(revision)},
                       {QStringLiteral("context_sha256"), opportunity.value(QStringLiteral("context_sha256"))},
                       {QStringLiteral("opportunity"), opportunity},
                       {QStringLiteral("candidates"), QJsonArray{
                           QJsonObject{{QStringLiteral("visual_anchor_id"), QStringLiteral("bin:10:frame:90")},
                                       {QStringLiteral("embedding_id"), QStringLiteral("emb-1")},
                                       {QStringLiteral("source_id"), QStringLiteral("bin:10")},
                                       {QStringLiteral("source_fingerprint"), QStringLiteral("fp-visual")},
                                       {QStringLiteral("sample_frame"), 90},
                                       {QStringLiteral("similarity"), 0.84},
                                       {QStringLiteral("score_semantics"), QStringLiteral("siglip_cosine_similarity_same_embedding_space_not_probability")}},
                       }}};
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

TEST_CASE("B-roll opportunity resolves exact A-roll range and grants no mutation authority", "[vibecut][broll][opportunity]")
{
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(
        QList<VibeCutMediaDocument>{doc(QStringLiteral("answer"), 100, 180, QStringLiteral("The turbine housing is cast from aluminum."))},
        14, 20, 600, &error);
    REQUIRE(error.isEmpty());
    const QJsonObject opportunity = validateVibeCutBrollOpportunity(
        context, QStringLiteral("answer"), QStringLiteral("aluminum turbine housing casting"),
        QStringLiteral("show_process"), 14, &error);
    REQUIRE(error.isEmpty());
    CHECK(opportunity.value(QStringLiteral("authority")).toString() == QStringLiteral("proposal"));
    CHECK(opportunity.value(QStringLiteral("target_start_frame")).toInt() == 100);
    CHECK(opportunity.value(QStringLiteral("target_end_frame")).toInt() == 180);
    CHECK(opportunity.value(QStringLiteral("target_duration_frames")).toInt() == 80);
    CHECK(opportunity.value(QStringLiteral("opportunity_id")).toString().size() == 64);
    CHECK_FALSE(opportunity.value(QStringLiteral("executable")).toBool(true));
    CHECK(opportunity.value(QStringLiteral("mutation_authority")).toString() == QStringLiteral("none"));
}

TEST_CASE("B-roll opportunity rejects invented candidate and unsupported purpose", "[vibecut][broll][opportunity][integrity]")
{
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(
        QList<VibeCutMediaDocument>{doc(QStringLiteral("answer"), 10, 50, QStringLiteral("Answer"))}, 3, 20, 600, &error);
    REQUIRE(error.isEmpty());
    CHECK(validateVibeCutBrollOpportunity(context, QStringLiteral("invented"), QStringLiteral("engine"),
                                           QStringLiteral("show_detail"), 3, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("unknown"), Qt::CaseInsensitive));
    error.clear();
    CHECK(validateVibeCutBrollOpportunity(context, QStringLiteral("answer"), QStringLiteral("engine"),
                                           QStringLiteral("invent_a_mode"), 3, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("unsupported"), Qt::CaseInsensitive));
}

TEST_CASE("B-roll placement selects only an exact returned visual anchor and does not invent source excerpt", "[vibecut][broll][placement]")
{
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(
        QList<VibeCutMediaDocument>{doc(QStringLiteral("answer"), 100, 180, QStringLiteral("The turbine housing is cast."))},
        9, 20, 600, &error);
    REQUIRE(error.isEmpty());
    const QJsonObject opportunity = validateVibeCutBrollOpportunity(
        context, QStringLiteral("answer"), QStringLiteral("turbine housing casting"),
        QStringLiteral("show_process"), 9, &error);
    REQUIRE(error.isEmpty());
    const QJsonObject search = searchResult(opportunity, 9);
    const QJsonObject placement = buildVibeCutBrollPlacementProposal(
        opportunity, search, QStringLiteral("bin:10:frame:90"), 9, &error);
    REQUIRE(error.isEmpty());
    CHECK(placement.value(QStringLiteral("authority")).toString() == QStringLiteral("proposal"));
    CHECK(placement.value(QStringLiteral("required_duration_frames")).toInt() == 80);
    CHECK(placement.value(QStringLiteral("visual_source_id")).toString() == QStringLiteral("bin:10"));
    CHECK(placement.value(QStringLiteral("visual_sample_frame")).toInt() == 90);
    CHECK(placement.value(QStringLiteral("source_excerpt_resolution")).toString() == QStringLiteral("not_resolved"));
    CHECK_FALSE(placement.contains(QStringLiteral("source_in_frame")));
    CHECK_FALSE(placement.contains(QStringLiteral("source_out_frame")));
    CHECK_FALSE(placement.value(QStringLiteral("executable")).toBool(true));
    CHECK(placement.value(QStringLiteral("mutation_authority")).toString() == QStringLiteral("none"));

    error.clear();
    CHECK(buildVibeCutBrollPlacementProposal(opportunity, search, QStringLiteral("not-returned"), 9, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("exactly once"), Qt::CaseInsensitive));
}

TEST_CASE("B-roll tools expose proposal and retrieval intent without raw media geometry", "[vibecut][broll][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    const QStringList names{QStringLiteral("broll_opportunity_validate"), QStringLiteral("broll_candidate_search"),
                            QStringLiteral("broll_candidate_result"), QStringLiteral("broll_placement_plan_validate")};
    for (const QString &name : names) {
        INFO(name.toStdString());
        REQUIRE(policies.contains(name));
        CHECK(policies.value(name).risk == VibeCutToolRisk::ReadOnly);
        CHECK_FALSE(policies.value(name).mutatesProject);
        CHECK_FALSE(schemaByName(surface, name).isEmpty());
    }
    CHECK(policies.value(QStringLiteral("broll_candidate_search")).asynchronous);

    const QJsonObject placementSchema = schemaByName(surface, QStringLiteral("broll_placement_plan_validate"));
    const QJsonObject placementProps = placementSchema.value(QStringLiteral("input_schema")).toObject()
                                           .value(QStringLiteral("properties")).toObject();
    CHECK(placementProps.contains(QStringLiteral("search_job_id")));
    CHECK(placementProps.contains(QStringLiteral("selected_visual_anchor_id")));
    CHECK_FALSE(placementProps.contains(QStringLiteral("source_path")));
    CHECK_FALSE(placementProps.contains(QStringLiteral("source_in_frame")));
    CHECK_FALSE(placementProps.contains(QStringLiteral("source_out_frame")));
    CHECK_FALSE(placementProps.contains(QStringLiteral("target_start_frame")));
    CHECK_FALSE(placementProps.contains(QStringLiteral("target_end_frame")));
}

TEST_CASE("B-roll direct opportunity invocation rejects fractional revisions before project lookup", "[vibecut][broll][integrity]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const QJsonObject result = surface.invoke(QStringLiteral("broll_opportunity_validate"),
        QJsonObject{{QStringLiteral("base_revision"), 0.5},
                    {QStringLiteral("context_sha256"), QString(64, QLatin1Char('a'))},
                    {QStringLiteral("context_max_candidates"), 200},
                    {QStringLiteral("context_max_text_chars"), 600},
                    {QStringLiteral("anchor_candidate_id"), QStringLiteral("a")},
                    {QStringLiteral("query"), QStringLiteral("engine")},
                    {QStringLiteral("purpose"), QStringLiteral("show_detail")}});
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool(true));
    CHECK(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("base_revision")));
    CHECK(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("integer"), Qt::CaseInsensitive));
}
