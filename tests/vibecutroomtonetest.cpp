/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutroomtone.h"

namespace {
QJsonObject loudness(int frame,
                     double pts,
                     double lufs,
                     const QString &fingerprint = QStringLiteral("fp-a"))
{
    return QJsonObject{
        {QStringLiteral("source_id"), QStringLiteral("bin:1")},
        {QStringLiteral("source_fingerprint"), fingerprint},
        {QStringLiteral("extractor_id"), QStringLiteral("audio_r128")},
        {QStringLiteral("extractor_version"), QStringLiteral("1.0.0")},
        {QStringLiteral("kind"), QStringLiteral("audio_loudness_sample")},
        {QStringLiteral("start_frame"), frame},
        {QStringLiteral("end_frame"), frame + 3},
        {QStringLiteral("confidence"), 1.0},
        {QStringLiteral("metadata"), QJsonObject{
            {QStringLiteral("sample_pts_seconds"), pts},
            {QStringLiteral("sample_interval_ms"), 500},
            {QStringLiteral("momentary_lufs"), lufs},
            {QStringLiteral("true_peak_dbfs"), -18.0},
        }},
    };
}

QJsonObject silence(int start, int end, const QString &fingerprint = QStringLiteral("fp-a"))
{
    return QJsonObject{
        {QStringLiteral("source_id"), QStringLiteral("bin:1")},
        {QStringLiteral("source_fingerprint"), fingerprint},
        {QStringLiteral("extractor_id"), QStringLiteral("silence_detect")},
        {QStringLiteral("extractor_version"), QStringLiteral("1.0.0")},
        {QStringLiteral("kind"), QStringLiteral("silence")},
        {QStringLiteral("start_frame"), start},
        {QStringLiteral("end_frame"), end},
        {QStringLiteral("confidence"), 1.0},
    };
}
}

TEST_CASE("room-tone inference requires stable consecutive measured observations", "[vibecut][audio][room-tone]")
{
    const QJsonArray records{
        loudness(15, 0.5, -44.0),
        loudness(30, 1.0, -43.5),
        loudness(45, 1.5, -44.8),
        loudness(60, 2.0, -43.9),
    };
    const QJsonArray candidates = buildVibeCutRoomToneCandidates(records, QStringLiteral("bin:1"), -65.0, -25.0, 5.0, 4);
    REQUIRE(candidates.size() == 1);
    const QJsonObject candidate = candidates.at(0).toObject();
    CHECK(candidate.value(QStringLiteral("authority")).toString() == QStringLiteral("derived_candidate"));
    CHECK(candidate.value(QStringLiteral("candidate_kind")).toString() == QStringLiteral("room_tone"));
    CHECK(candidate.value(QStringLiteral("observation_count")).toInt() == 4);
    CHECK(candidate.value(QStringLiteral("observed_frames")).toArray() == QJsonArray{15, 30, 45, 60});
    CHECK(candidate.value(QStringLiteral("momentary_spread_lu")).toDouble() < 2.0);
}

TEST_CASE("room-tone inference excludes stored silence and never bridges through it", "[vibecut][audio][room-tone][silence]")
{
    const QJsonArray records{
        loudness(0, 0.0, -45.0),
        loudness(15, 0.5, -45.2),
        loudness(30, 1.0, -45.1),
        loudness(45, 1.5, -44.9),
        loudness(60, 2.0, -45.0),
        silence(30, 33),
    };
    const QJsonArray candidates = buildVibeCutRoomToneCandidates(records, QStringLiteral("bin:1"), -65.0, -25.0, 5.0, 2);
    REQUIRE(candidates.size() == 2);
    CHECK(candidates.at(0).toObject().value(QStringLiteral("observed_frames")).toArray() == QJsonArray{0, 15});
    CHECK(candidates.at(1).toObject().value(QStringLiteral("observed_frames")).toArray() == QJsonArray{45, 60});
}

TEST_CASE("room-tone inference never crosses source fingerprints", "[vibecut][audio][room-tone][provenance]")
{
    const QJsonArray records{
        loudness(0, 0.0, -45.0, QStringLiteral("fp-a")),
        loudness(15, 0.5, -45.1, QStringLiteral("fp-a")),
        loudness(30, 1.0, -45.0, QStringLiteral("fp-b")),
        loudness(45, 1.5, -45.1, QStringLiteral("fp-b")),
    };
    const QJsonArray candidates = buildVibeCutRoomToneCandidates(records, QString(), -65.0, -25.0, 5.0, 2);
    REQUIRE(candidates.size() == 2);
    CHECK(candidates.at(0).toObject().value(QStringLiteral("source_fingerprint")).toString() !=
          candidates.at(1).toObject().value(QStringLiteral("source_fingerprint")).toString());
}

TEST_CASE("room-tone inference splits when measured loudness stability is lost", "[vibecut][audio][room-tone][stability]")
{
    const QJsonArray records{
        loudness(0, 0.0, -45.0),
        loudness(15, 0.5, -44.0),
        loudness(30, 1.0, -43.0),
        loudness(45, 1.5, -31.0),
        loudness(60, 2.0, -30.5),
    };
    const QJsonArray candidates = buildVibeCutRoomToneCandidates(records, QStringLiteral("bin:1"), -65.0, -25.0, 5.0, 3);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates.at(0).toObject().value(QStringLiteral("observed_frames")).toArray() == QJsonArray{0, 15, 30});
}
