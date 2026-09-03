/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutactionsummary.h"

namespace {
QJsonObject prediction(int start, int end, double score,
                       const QString &label = QStringLiteral("walking"),
                       const QString &fingerprint = QStringLiteral("fp-a"),
                       const QString &hash = QStringLiteral("hash-a"))
{
    QJsonArray frames;
    for (int i = 0; i < 8; ++i) frames.append(start + i * qMax(1, (end - start - 1) / 7));
    return QJsonObject{
        {QStringLiteral("source_id"), QStringLiteral("bin:1")},
        {QStringLiteral("source_fingerprint"), fingerprint},
        {QStringLiteral("extractor_id"), QStringLiteral("local_xclip_actions")},
        {QStringLiteral("extractor_version"), QStringLiteral("1.0.0")},
        {QStringLiteral("kind"), QStringLiteral("action_prediction")},
        {QStringLiteral("start_frame"), start},
        {QStringLiteral("end_frame"), end},
        {QStringLiteral("confidence"), score},
        {QStringLiteral("metadata"), QJsonObject{
            {QStringLiteral("label"), label},
            {QStringLiteral("prompt"), QStringLiteral("a video of a person %1").arg(label)},
            {QStringLiteral("label_id"), 4},
            {QStringLiteral("rank"), 1},
            {QStringLiteral("window_start_frame"), start},
            {QStringLiteral("window_end_frame"), end},
            {QStringLiteral("observed_frames"), frames},
            {QStringLiteral("model"), QStringLiteral("microsoft/xclip-base-patch32")},
            {QStringLiteral("model_revision"), QStringLiteral("rev-a")},
            {QStringLiteral("taxonomy"), QStringLiteral("VibeCutActionSet-v1")},
            {QStringLiteral("action_set_sha256"), hash},
            {QStringLiteral("score_semantics"), QStringLiteral("softmax_over_fixed_action_set")},
            {QStringLiteral("authority"), QStringLiteral("model_prediction")},
        }},
    };
}
}

TEST_CASE("action summaries merge overlapping same-label windows while retaining sample support", "[vibecut][actions][summary]")
{
    const QJsonArray records{
        prediction(0, 120, 0.61),
        prediction(60, 180, 0.73),
        prediction(120, 240, 0.66),
    };
    const QJsonArray summaries = buildVibeCutActionSummaries(records, QStringLiteral("bin:1"), QStringLiteral("walking"), 0.5, 0, 2);
    REQUIRE(summaries.size() == 1);
    const QJsonObject summary = summaries.at(0).toObject();
    CHECK(summary.value(QStringLiteral("authority")).toString() == QStringLiteral("derived_prediction_summary"));
    CHECK(summary.value(QStringLiteral("window_count")).toInt() == 3);
    CHECK(summary.value(QStringLiteral("prediction_windows")).toArray().size() == 3);
    CHECK(summary.value(QStringLiteral("supporting_observed_frames")).toArray().size() >= 8);
    CHECK(summary.value(QStringLiteral("score_semantics")).toString() == QStringLiteral("softmax_over_fixed_action_set"));
    CHECK(summary.value(QStringLiteral("note")).toString().contains(QStringLiteral("not factual probabilities")));
}

TEST_CASE("action summaries never cross source fingerprints or action-set hashes", "[vibecut][actions][summary][provenance]")
{
    const QJsonArray records{
        prediction(0, 120, 0.7, QStringLiteral("walking"), QStringLiteral("fp-a"), QStringLiteral("hash-a")),
        prediction(60, 180, 0.7, QStringLiteral("walking"), QStringLiteral("fp-a"), QStringLiteral("hash-b")),
        prediction(120, 240, 0.7, QStringLiteral("walking"), QStringLiteral("fp-b"), QStringLiteral("hash-a")),
    };
    const QJsonArray summaries = buildVibeCutActionSummaries(records, QString(), QString(), 0.5, 0, 1);
    REQUIRE(summaries.size() == 3);
}

TEST_CASE("action summaries split non-overlapping predictions unless an explicit gap is allowed", "[vibecut][actions][summary][gap]")
{
    const QJsonArray records{prediction(0, 100, 0.7), prediction(120, 220, 0.8)};
    CHECK(buildVibeCutActionSummaries(records, QString(), QString(), 0.5, 0, 1).size() == 2);
    CHECK(buildVibeCutActionSummaries(records, QString(), QString(), 0.5, 20, 1).size() == 1);
}
