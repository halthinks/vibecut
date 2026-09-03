/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutocrtemporal.h"

namespace {
QJsonObject ocrRecord(int frame,
                      const QString &text,
                      int x = 100,
                      const QString &fingerprint = QStringLiteral("fp-a"),
                      double confidence = 0.9)
{
    return QJsonObject{
        {QStringLiteral("source_id"), QStringLiteral("bin:1")},
        {QStringLiteral("source_fingerprint"), fingerprint},
        {QStringLiteral("extractor_id"), QStringLiteral("local_tesseract_ocr")},
        {QStringLiteral("extractor_version"), QStringLiteral("1.0.0/tesseract_5")},
        {QStringLiteral("kind"), QStringLiteral("ocr_text")},
        {QStringLiteral("start_frame"), frame},
        {QStringLiteral("end_frame"), frame + 1},
        {QStringLiteral("text"), text},
        {QStringLiteral("confidence"), confidence},
        {QStringLiteral("metadata"), QJsonObject{
            {QStringLiteral("sample_frame"), frame},
            {QStringLiteral("sample_interval_frames"), 30},
            {QStringLiteral("image_width"), 1920},
            {QStringLiteral("image_height"), 1080},
            {QStringLiteral("bbox_pixels"), QJsonObject{{QStringLiteral("x"), x}, {QStringLiteral("y"), 900},
                                                         {QStringLiteral("width"), 500}, {QStringLiteral("height"), 80}}},
            {QStringLiteral("language"), QStringLiteral("eng")},
            {QStringLiteral("engine"), QStringLiteral("tesseract")},
        }},
    };
}
}

TEST_CASE("OCR temporal tracks preserve observed frames and label intervening time as inference", "[vibecut][ocr][temporal]")
{
    const QJsonArray records{
        ocrRecord(0, QStringLiteral("Launch Date"), 100, QStringLiteral("fp-a"), 0.88),
        ocrRecord(30, QStringLiteral("LAUNCH DATE"), 105, QStringLiteral("fp-a"), 0.94),
        ocrRecord(60, QStringLiteral("Launch Date"), 102, QStringLiteral("fp-a"), 0.91),
    };
    const QJsonArray tracks = buildVibeCutOcrTemporalTracks(records, 0.85, 0.25, 0, 2);
    REQUIRE(tracks.size() == 1);
    const QJsonObject track = tracks.at(0).toObject();
    CHECK(track.value(QStringLiteral("observation_count")).toInt() == 3);
    CHECK(track.value(QStringLiteral("first_observed_frame")).toInt() == 0);
    CHECK(track.value(QStringLiteral("last_observed_frame")).toInt() == 60);
    CHECK(track.value(QStringLiteral("authority")).toString() == QStringLiteral("inferred_temporal_track"));
    CHECK_FALSE(track.value(QStringLiteral("fully_observed")).toBool(true));
    CHECK(track.value(QStringLiteral("unobserved_gap_frames")).toInt() == 58);
    const QJsonArray observed = track.value(QStringLiteral("observed_frames")).toArray();
    CHECK(observed == QJsonArray{0, 30, 60});
}

TEST_CASE("OCR temporal tracks never cross source fingerprint lineage", "[vibecut][ocr][temporal][provenance]")
{
    const QJsonArray records{
        ocrRecord(0, QStringLiteral("SALE"), 100, QStringLiteral("fp-a")),
        ocrRecord(30, QStringLiteral("SALE"), 100, QStringLiteral("fp-a")),
        ocrRecord(60, QStringLiteral("SALE"), 100, QStringLiteral("fp-b")),
        ocrRecord(90, QStringLiteral("SALE"), 100, QStringLiteral("fp-b")),
    };
    const QJsonArray tracks = buildVibeCutOcrTemporalTracks(records, 0.85, 0.25, 0, 2);
    REQUIRE(tracks.size() == 2);
    CHECK(tracks.at(0).toObject().value(QStringLiteral("source_fingerprint")).toString() !=
          tracks.at(1).toObject().value(QStringLiteral("source_fingerprint")).toString());
}

TEST_CASE("OCR temporal matching requires geometry continuity and bounded sample gaps", "[vibecut][ocr][temporal][geometry]")
{
    QJsonArray geometrySeparated{
        ocrRecord(0, QStringLiteral("LIVE"), 100),
        ocrRecord(30, QStringLiteral("LIVE"), 1300),
    };
    CHECK(buildVibeCutOcrTemporalTracks(geometrySeparated, 0.85, 0.25, 0, 2).isEmpty());

    QJsonArray missingSample{
        ocrRecord(0, QStringLiteral("LIVE"), 100),
        ocrRecord(60, QStringLiteral("LIVE"), 100),
    };
    CHECK(buildVibeCutOcrTemporalTracks(missingSample, 0.85, 0.25, 0, 2).isEmpty());
    CHECK(buildVibeCutOcrTemporalTracks(missingSample, 0.85, 0.25, 1, 2).size() == 1);
}
