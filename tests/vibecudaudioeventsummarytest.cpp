/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecudaudioeventsummary.h"

namespace {
QJsonObject prediction(const QString &id,
                       const QString &label,
                       int labelId,
                       int start,
                       int end,
                       double score,
                       int rank = 1,
                       const QString &fingerprint = QStringLiteral("fp-a"),
                       const QString &model = QStringLiteral("MIT/ast-finetuned-audioset-10-10-0.4593"))
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("source_id"), QStringLiteral("bin:1")},
        {QStringLiteral("source_fingerprint"), fingerprint},
        {QStringLiteral("extractor_id"), QStringLiteral("local_ast_audioset")},
        {QStringLiteral("extractor_version"), QStringLiteral("1.0.0")},
        {QStringLiteral("kind"), QStringLiteral("audio_event_prediction")},
        {QStringLiteral("start_frame"), start},
        {QStringLiteral("end_frame"), end},
        {QStringLiteral("confidence"), score},
        {QStringLiteral("metadata"), QJsonObject{
            {QStringLiteral("authority"), QStringLiteral("model_prediction")},
            {QStringLiteral("taxonomy"), QStringLiteral("AudioSet")},
            {QStringLiteral("model"), model},
            {QStringLiteral("label"), label},
            {QStringLiteral("label_id"), labelId},
            {QStringLiteral("rank"), rank},
        }},
    };
}
}

TEST_CASE("audio-event tracks merge overlapping same-label predictions without promoting authority", "[vibecut][audio-events][summary]")
{
    const QJsonArray records{
        prediction(QStringLiteral("a"), QStringLiteral("Speech"), 0, 0, 250, 0.72),
        prediction(QStringLiteral("b"), QStringLiteral("Speech"), 0, 125, 375, 0.84),
        prediction(QStringLiteral("c"), QStringLiteral("Speech"), 0, 250, 500, 0.66),
    };
    const QJsonArray tracks = buildVibeCutAudioEventTracks(records, QStringLiteral("bin:1"), QStringLiteral("speech"), 0.10, 8, 0);
    REQUIRE(tracks.size() == 1);
    const QJsonObject track = tracks.at(0).toObject();
    CHECK(track.value(QStringLiteral("authority")).toString() == QStringLiteral("derived_prediction_summary"));
    CHECK(track.value(QStringLiteral("label")).toString() == QStringLiteral("Speech"));
    CHECK(track.value(QStringLiteral("start_frame")).toInt() == 0);
    CHECK(track.value(QStringLiteral("end_frame")).toInt() == 500);
    CHECK(track.value(QStringLiteral("prediction_window_count")).toInt() == 3);
    CHECK(track.value(QStringLiteral("peak_score")).toDouble() == Approx(0.84));
    CHECK(track.value(QStringLiteral("mean_score")).toDouble() == Approx((0.72 + 0.84 + 0.66) / 3.0));
    CHECK(track.value(QStringLiteral("prediction_record_ids")).toArray() == QJsonArray{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
}

TEST_CASE("audio-event tracks never cross source fingerprints or model provenance", "[vibecut][audio-events][summary][provenance]")
{
    const QJsonArray records{
        prediction(QStringLiteral("a"), QStringLiteral("Music"), 137, 0, 250, 0.60, 1, QStringLiteral("fp-a")),
        prediction(QStringLiteral("b"), QStringLiteral("Music"), 137, 125, 375, 0.70, 1, QStringLiteral("fp-b")),
        prediction(QStringLiteral("c"), QStringLiteral("Music"), 137, 250, 500, 0.80, 1, QStringLiteral("fp-b"), QStringLiteral("other-model")),
    };
    const QJsonArray tracks = buildVibeCutAudioEventTracks(records, QStringLiteral("bin:1"), QStringLiteral("music"), 0.10, 8, 0);
    REQUIRE(tracks.size() == 3);
    CHECK(tracks.at(0).toObject().value(QStringLiteral("source_fingerprint")).toString() !=
          tracks.at(1).toObject().value(QStringLiteral("source_fingerprint")).toString());
    CHECK(tracks.at(1).toObject().value(QStringLiteral("model")).toString() !=
          tracks.at(2).toObject().value(QStringLiteral("model")).toString());
}

TEST_CASE("audio-event summary filters by label score and rank", "[vibecut][audio-events][summary][filter]")
{
    const QJsonArray records{
        prediction(QStringLiteral("speech"), QStringLiteral("Speech"), 0, 0, 250, 0.40, 1),
        prediction(QStringLiteral("music"), QStringLiteral("Background music"), 267, 0, 250, 0.35, 2),
        prediction(QStringLiteral("wind"), QStringLiteral("Wind"), 283, 0, 250, 0.08, 3),
        prediction(QStringLiteral("noise"), QStringLiteral("Noise"), 513, 0, 250, 0.30, 9),
    };

    QJsonArray tracks = buildVibeCutAudioEventTracks(records, QStringLiteral("bin:1"), QStringLiteral("music"), 0.10, 8, 0);
    REQUIRE(tracks.size() == 1);
    CHECK(tracks.at(0).toObject().value(QStringLiteral("label")).toString() == QStringLiteral("Background music"));

    tracks = buildVibeCutAudioEventTracks(records, QStringLiteral("bin:1"), QString(), 0.10, 2, 0);
    REQUIRE(tracks.size() == 2);
    CHECK(tracks.at(0).toObject().value(QStringLiteral("label")).toString() == QStringLiteral("Speech"));
    CHECK(tracks.at(1).toObject().value(QStringLiteral("label")).toString() == QStringLiteral("Background music"));
}

TEST_CASE("audio-event tracks require explicit gap allowance to bridge separated prediction windows", "[vibecut][audio-events][summary][gap]")
{
    const QJsonArray records{
        prediction(QStringLiteral("a"), QStringLiteral("Speech"), 0, 0, 100, 0.70),
        prediction(QStringLiteral("b"), QStringLiteral("Speech"), 0, 110, 210, 0.75),
    };
    CHECK(buildVibeCutAudioEventTracks(records, QStringLiteral("bin:1"), QStringLiteral("speech"), 0.10, 8, 0).size() == 2);
    CHECK(buildVibeCutAudioEventTracks(records, QStringLiteral("bin:1"), QStringLiteral("speech"), 0.10, 8, 10).size() == 1);
}
