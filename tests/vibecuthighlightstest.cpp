/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecuthighlights.h"
#include "vibecut/vibecutroughcutsynthesis.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
VibeCutMediaDocument doc(const QString &id, int start, int end, const QString &text,
                         const QString &source = QStringLiteral("timeline:abc"),
                         const QString &fingerprint = QStringLiteral("fp-a"))
{
    VibeCutMediaDocument document;
    document.id = id;
    document.kind = QStringLiteral("transcript_segment");
    document.startFrame = start;
    document.endFrame = end;
    document.text = text;
    document.metadata = QJsonObject{{QStringLiteral("evidence_origin"), QStringLiteral("extractor")},
                                    {QStringLiteral("source_id"), source},
                                    {QStringLiteral("source_fingerprint"), fingerprint},
                                    {QStringLiteral("extractor_id"), QStringLiteral("whisper_transcript")},
                                    {QStringLiteral("extractor_version"), QStringLiteral("model:turbo")}};
    return document;
}

QJsonObject rankingFor(const QJsonObject &context, quint64 revision,
                       const QList<QPair<QString, double>> &scores)
{
    QHash<QString, QJsonObject> byId;
    for (const QJsonValue &value : context.value(QStringLiteral("candidates")).toArray()) {
        const QJsonObject candidate = value.toObject();
        byId.insert(candidate.value(QStringLiteral("candidate_id")).toString(), candidate);
    }
    QJsonArray candidates;
    int rank = 1;
    for (const auto &entry : scores) {
        QJsonObject candidate = byId.value(entry.first);
        candidate.insert(QStringLiteral("objective_relevance_score"), entry.second);
        candidate.insert(QStringLiteral("rank"), rank++);
        candidates.append(candidate);
    }
    return QJsonObject{{QStringLiteral("kind"), QStringLiteral("rough_cut_objective_rank")},
                       {QStringLiteral("authority"), QStringLiteral("derived_ranking")},
                       {QStringLiteral("score_semantics"), QStringLiteral("current_hybrid_relevance_not_probability")},
                       {QStringLiteral("base_revision"), static_cast<qint64>(revision)},
                       {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                       {QStringLiteral("objective"), QStringLiteral("Find the clearest launch explanation")},
                       {QStringLiteral("candidates"), candidates}};
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

TEST_CASE("highlight proposal selects ranked non-overlapping candidates within budget", "[vibecut][highlights][proposal]")
{
    QList<VibeCutMediaDocument> documents{
        doc(QStringLiteral("a"), 0, 40, QStringLiteral("Opening launch explanation")),
        doc(QStringLiteral("b"), 30, 70, QStringLiteral("Overlapping alternative")),
        doc(QStringLiteral("c"), 80, 120, QStringLiteral("Strong follow-up detail")),
        doc(QStringLiteral("d"), 140, 200, QStringLiteral("Long lower-ranked detail")),
    };
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(documents, 12, 50, 600, &error);
    REQUIRE(error.isEmpty());
    const QJsonObject ranking = rankingFor(context, 12, {{QStringLiteral("b"), 0.98},
                                                         {QStringLiteral("a"), 0.95},
                                                         {QStringLiteral("c"), 0.90},
                                                         {QStringLiteral("d"), 0.70}});
    const QJsonObject proposal = buildVibeCutHighlightProposal(context, ranking, QStringLiteral("short"),
                                                               3, 90, 0.75, true, 12, &error);
    REQUIRE(error.isEmpty());
    CHECK(proposal.value(QStringLiteral("authority")).toString() == QStringLiteral("proposal"));
    CHECK_FALSE(proposal.value(QStringLiteral("executable")).toBool(true));
    CHECK(proposal.value(QStringLiteral("mutation_authority")).toString() == QStringLiteral("none"));
    CHECK(proposal.value(QStringLiteral("format")).toString() == QStringLiteral("short"));
    CHECK(proposal.value(QStringLiteral("segment_count")).toInt() == 2);
    CHECK(proposal.value(QStringLiteral("total_frames")).toInt() == 80);
    CHECK(proposal.value(QStringLiteral("overlap_skipped")).toInt() == 1);
    CHECK(proposal.value(QStringLiteral("below_relevance_skipped")).toInt() == 1);
    const QJsonArray selected = proposal.value(QStringLiteral("selected_candidate_ids")).toArray();
    REQUIRE(selected.size() == 2);
    // b is highest-ranked; c is non-overlapping and fits. Source-order output is retained.
    CHECK(selected.at(0).toString() == QStringLiteral("b"));
    CHECK(selected.at(1).toString() == QStringLiteral("c"));
}

TEST_CASE("highlight proposal can retain relevance order instead of source order", "[vibecut][highlights][ordering]")
{
    QList<VibeCutMediaDocument> documents{
        doc(QStringLiteral("early"), 0, 30, QStringLiteral("Earlier segment")),
        doc(QStringLiteral("late"), 100, 130, QStringLiteral("Later stronger segment")),
    };
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(documents, 5, 20, 600, &error);
    REQUIRE(error.isEmpty());
    const QJsonObject ranking = rankingFor(context, 5, {{QStringLiteral("late"), 0.95}, {QStringLiteral("early"), 0.80}});
    const QJsonObject proposal = buildVibeCutHighlightProposal(context, ranking, QStringLiteral("highlight_reel"),
                                                               2, 100, 0.0, false, 5, &error);
    REQUIRE(error.isEmpty());
    const QJsonArray selected = proposal.value(QStringLiteral("selected_candidate_ids")).toArray();
    REQUIRE(selected.size() == 2);
    CHECK(selected.at(0).toString() == QStringLiteral("late"));
    CHECK(selected.at(1).toString() == QStringLiteral("early"));
}

TEST_CASE("highlight proposal rejects provenance drift rather than coercing it", "[vibecut][highlights][provenance]")
{
    QList<VibeCutMediaDocument> documents{doc(QStringLiteral("a"), 0, 40, QStringLiteral("Candidate"))};
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(documents, 8, 20, 600, &error);
    REQUIRE(error.isEmpty());
    QJsonObject ranking = rankingFor(context, 8, {{QStringLiteral("a"), 0.95}});
    QJsonArray ranked = ranking.value(QStringLiteral("candidates")).toArray();
    QJsonObject tampered = ranked.at(0).toObject();
    tampered.insert(QStringLiteral("source_fingerprint"), QStringLiteral("different"));
    ranked.replace(0, tampered);
    ranking.insert(QStringLiteral("candidates"), ranked);
    const QJsonObject proposal = buildVibeCutHighlightProposal(context, ranking, QStringLiteral("quote"),
                                                               1, 100, 0.0, true, 8, &error);
    CHECK(proposal.isEmpty());
    CHECK(error.contains(QStringLiteral("No objective-ranked"), Qt::CaseInsensitive));
}

TEST_CASE("highlight tool is read-only and accepts no raw edit geometry", "[vibecut][highlights][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("highlight_proposal_build")));
    const VibeCutToolPolicy policy = policies.value(QStringLiteral("highlight_proposal_build"));
    CHECK(policy.risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(policy.asynchronous);
    CHECK_FALSE(policy.mutatesProject);

    const QJsonObject schema = schemaByName(surface, QStringLiteral("highlight_proposal_build"));
    REQUIRE_FALSE(schema.isEmpty());
    const QJsonObject properties = schema.value(QStringLiteral("input_schema")).toObject()
                                       .value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("objective_job_id")));
    CHECK(properties.contains(QStringLiteral("format")));
    CHECK(properties.contains(QStringLiteral("max_total_frames")));
    CHECK_FALSE(properties.contains(QStringLiteral("start_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("end_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("source_path")));
    CHECK_FALSE(properties.contains(QStringLiteral("operations")));
}

TEST_CASE("highlight direct invocation rejects fractional integer fields before project lookup", "[vibecut][highlights][integrity]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    QJsonObject input{{QStringLiteral("base_revision"), 0},
                      {QStringLiteral("context_sha256"), QString(64, QLatin1Char('a'))},
                      {QStringLiteral("context_max_candidates"), 200},
                      {QStringLiteral("context_max_text_chars"), 600},
                      {QStringLiteral("objective_job_id"), QStringLiteral("unused")},
                      {QStringLiteral("format"), QStringLiteral("short")},
                      {QStringLiteral("max_segments"), 2.5},
                      {QStringLiteral("max_total_frames"), 120}};
    const QJsonObject result = surface.invoke(QStringLiteral("highlight_proposal_build"), input);
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool(true));
    CHECK(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("max_segments")));
    CHECK(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("integer"), Qt::CaseInsensitive));
}
