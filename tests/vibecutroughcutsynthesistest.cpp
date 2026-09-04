/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutroughcutsynthesis.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
VibeCutMediaDocument doc(const QString &id, const QString &kind, int start, int end, const QString &text,
                         const QString &source = QString(), const QString &fingerprint = QString())
{
    VibeCutMediaDocument document;
    document.id = id;
    document.kind = kind;
    document.startFrame = start;
    document.endFrame = end;
    document.text = text;
    document.metadata = QJsonObject{{QStringLiteral("evidence_origin"), kind == QLatin1String("transcript_segment")
                                                                            ? QStringLiteral("extractor")
                                                                            : QStringLiteral("subtitle_track")}};
    if (!source.isEmpty()) document.metadata.insert(QStringLiteral("source_id"), source);
    if (!fingerprint.isEmpty()) document.metadata.insert(QStringLiteral("source_fingerprint"), fingerprint);
    return document;
}

QJsonObject proposalFor(const QJsonObject &context, quint64 revision, const QJsonArray &ids,
                        const QString &objective = QStringLiteral("Build a concise interview rough cut"))
{
    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("authority"), QStringLiteral("proposal")},
                       {QStringLiteral("base_revision"), static_cast<qint64>(revision)},
                       {QStringLiteral("context_sha256"), context.value(QStringLiteral("context_sha256"))},
                       {QStringLiteral("objective"), objective},
                       {QStringLiteral("selected_candidate_ids"), ids}};
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

TEST_CASE("rough-cut context admits only transcript candidates and prefers source-backed evidence", "[vibecut][rough-cut][context]")
{
    QList<VibeCutMediaDocument> documents{
        doc(QStringLiteral("subtitle:1"), QStringLiteral("transcript"), 10, 40, QStringLiteral("Hello world")),
        doc(QStringLiteral("evidence:whisper:1"), QStringLiteral("transcript_segment"), 10, 40, QStringLiteral("Hello   world"),
            QStringLiteral("timeline:abc"), QStringLiteral("fp-a")),
        doc(QStringLiteral("ocr:1"), QStringLiteral("ocr_text"), 20, 21, QStringLiteral("HELLO")),
        doc(QStringLiteral("clip:1"), QStringLiteral("clip"), 0, 100, QStringLiteral("camera-a.mp4")),
        doc(QStringLiteral("subtitle:2"), QStringLiteral("transcript"), 50, 90, QStringLiteral("Second answer")),
    };
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(documents, 17, 20, 600, &error);
    REQUIRE(error.isEmpty());
    CHECK(context.value(QStringLiteral("authority")).toString() == QStringLiteral("proposal_context"));
    CHECK(context.value(QStringLiteral("execution_authority")).toString() == QStringLiteral("none"));
    CHECK(context.value(QStringLiteral("base_revision")).toInt() == 17);
    CHECK(context.value(QStringLiteral("candidate_count")).toInt() == 2);
    CHECK(context.value(QStringLiteral("context_sha256")).toString().size() == 64);
    const QJsonArray candidates = context.value(QStringLiteral("candidates")).toArray();
    REQUIRE(candidates.size() == 2);
    CHECK(candidates.at(0).toObject().value(QStringLiteral("candidate_id")).toString() == QStringLiteral("evidence:whisper:1"));
    CHECK(candidates.at(0).toObject().value(QStringLiteral("source_fingerprint")).toString() == QStringLiteral("fp-a"));
    CHECK(candidates.at(1).toObject().value(QStringLiteral("candidate_id")).toString() == QStringLiteral("subtitle:2"));
}

TEST_CASE("validated rough-cut proposal resolves exact candidate ranges but grants no mutation authority", "[vibecut][rough-cut][proposal]")
{
    QList<VibeCutMediaDocument> documents{
        doc(QStringLiteral("a"), QStringLiteral("transcript"), 10, 40, QStringLiteral("Opening")),
        doc(QStringLiteral("b"), QStringLiteral("transcript"), 50, 100, QStringLiteral("Main point")),
    };
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(documents, 42, 20, 600, &error);
    REQUIRE(error.isEmpty());
    QJsonObject proposal = proposalFor(context, 42, QJsonArray{QStringLiteral("b"), QStringLiteral("a")});
    proposal.insert(QStringLiteral("max_total_frames"), 100);
    const QJsonObject result = validateVibeCutRoughCutProposal(context, proposal, 42, &error);
    REQUIRE(error.isEmpty());
    CHECK(result.value(QStringLiteral("authority")).toString() == QStringLiteral("proposal"));
    CHECK_FALSE(result.value(QStringLiteral("executable")).toBool(true));
    CHECK(result.value(QStringLiteral("mutation_authority")).toString() == QStringLiteral("none"));
    CHECK(result.value(QStringLiteral("segment_count")).toInt() == 2);
    CHECK(result.value(QStringLiteral("total_frames")).toInt() == 80);
    CHECK(result.value(QStringLiteral("reorders_timeline")).toBool(false));
    const QJsonArray segments = result.value(QStringLiteral("segments")).toArray();
    REQUIRE(segments.size() == 2);
    CHECK(segments.at(0).toObject().value(QStringLiteral("candidate_id")).toString() == QStringLiteral("b"));
    CHECK(segments.at(0).toObject().value(QStringLiteral("start_frame")).toInt() == 50);
    CHECK(segments.at(1).toObject().value(QStringLiteral("start_frame")).toInt() == 10);
}

TEST_CASE("rough-cut proposal fails closed on stale context tamper invented ids duplicates and duration overflow", "[vibecut][rough-cut][integrity]")
{
    QList<VibeCutMediaDocument> documents{
        doc(QStringLiteral("a"), QStringLiteral("transcript"), 0, 50, QStringLiteral("One")),
        doc(QStringLiteral("b"), QStringLiteral("transcript"), 50, 110, QStringLiteral("Two")),
    };
    QString error;
    const QJsonObject context = buildVibeCutRoughCutContext(documents, 7, 20, 600, &error);
    REQUIRE(error.isEmpty());

    QJsonObject stale = proposalFor(context, 7, QJsonArray{QStringLiteral("a")});
    CHECK(validateVibeCutRoughCutProposal(context, stale, 8, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("stale"), Qt::CaseInsensitive));

    error.clear();
    QJsonObject tamperedContext = context;
    QJsonArray changed = tamperedContext.value(QStringLiteral("candidates")).toArray();
    QJsonObject candidate = changed.at(0).toObject();
    candidate.insert(QStringLiteral("start_frame"), 2);
    changed.replace(0, candidate);
    tamperedContext.insert(QStringLiteral("candidates"), changed);
    CHECK(validateVibeCutRoughCutProposal(tamperedContext, proposalFor(tamperedContext, 7, QJsonArray{QStringLiteral("a")}), 7, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("identity"), Qt::CaseInsensitive));

    error.clear();
    CHECK(validateVibeCutRoughCutProposal(context, proposalFor(context, 7, QJsonArray{QStringLiteral("invented")}), 7, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("unknown"), Qt::CaseInsensitive));

    error.clear();
    CHECK(validateVibeCutRoughCutProposal(context, proposalFor(context, 7, QJsonArray{QStringLiteral("a"), QStringLiteral("a")}), 7, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("duplicate"), Qt::CaseInsensitive));

    error.clear();
    QJsonObject tooLong = proposalFor(context, 7, QJsonArray{QStringLiteral("a"), QStringLiteral("b")});
    tooLong.insert(QStringLiteral("max_total_frames"), 80);
    CHECK(validateVibeCutRoughCutProposal(context, tooLong, 7, &error).isEmpty());
    CHECK(error.contains(QStringLiteral("exceeds"), Qt::CaseInsensitive));
}

TEST_CASE("rough-cut tools are read-only and do not expose raw edit geometry", "[vibecut][rough-cut][surface]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    const auto policies = surface.policies();
    REQUIRE(policies.contains(QStringLiteral("rough_cut_context")));
    REQUIRE(policies.contains(QStringLiteral("rough_cut_proposal_validate")));
    CHECK(policies.value(QStringLiteral("rough_cut_context")).risk == VibeCutToolRisk::ReadOnly);
    CHECK(policies.value(QStringLiteral("rough_cut_proposal_validate")).risk == VibeCutToolRisk::ReadOnly);
    CHECK_FALSE(policies.value(QStringLiteral("rough_cut_proposal_validate")).mutatesProject);

    const QJsonObject schema = schemaByName(surface, QStringLiteral("rough_cut_proposal_validate"));
    REQUIRE_FALSE(schema.isEmpty());
    const QJsonObject properties = schema.value(QStringLiteral("input_schema")).toObject()
                                       .value(QStringLiteral("properties")).toObject();
    CHECK(properties.contains(QStringLiteral("selected_candidate_ids")));
    CHECK(properties.contains(QStringLiteral("objective")));
    CHECK_FALSE(properties.contains(QStringLiteral("start_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("end_frame")));
    CHECK_FALSE(properties.contains(QStringLiteral("source_path")));
    CHECK_FALSE(properties.contains(QStringLiteral("operations")));
}
