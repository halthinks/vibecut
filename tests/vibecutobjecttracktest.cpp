/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutobjecttracks.h"

namespace {
QJsonObject detection(int frame, int x, int y, int width, int height,
                      const QString &label = QStringLiteral("person"),
                      const QString &fingerprint = QStringLiteral("fp-a"),
                      double score = 0.9)
{
    return QJsonObject{
        {QStringLiteral("source_id"), QStringLiteral("bin:1")},
        {QStringLiteral("source_fingerprint"), fingerprint},
        {QStringLiteral("extractor_id"), QStringLiteral("local_detr_coco")},
        {QStringLiteral("extractor_version"), QStringLiteral("1.0.0")},
        {QStringLiteral("kind"), QStringLiteral("object_detection_prediction")},
        {QStringLiteral("start_frame"), frame},
        {QStringLiteral("end_frame"), frame + 1},
        {QStringLiteral("confidence"), score},
        {QStringLiteral("metadata"), QJsonObject{
            {QStringLiteral("sample_frame"), frame},
            {QStringLiteral("image_width"), 1000},
            {QStringLiteral("image_height"), 1000},
            {QStringLiteral("bbox_pixels"), QJsonObject{{QStringLiteral("x"), x}, {QStringLiteral("y"), y},
                                                         {QStringLiteral("width"), width}, {QStringLiteral("height"), height}}},
            {QStringLiteral("label"), label},
            {QStringLiteral("label_id"), label == QLatin1String("person") ? 1 : 3},
            {QStringLiteral("model"), QStringLiteral("facebook/detr-resnet-50")},
            {QStringLiteral("model_revision"), QStringLiteral("rev-a")},
            {QStringLiteral("taxonomy"), QStringLiteral("COCO-2017")},
            {QStringLiteral("authority"), QStringLiteral("model_prediction")},
            {QStringLiteral("sample_interval_frames"), 30},
        }},
    };
}
}

TEST_CASE("object tracks preserve sampled-frame authority and expose unobserved intervals", "[vibecut][objects][tracks]")
{
    const QJsonArray records{
        detection(0, 100, 100, 300, 500),
        detection(30, 110, 105, 300, 500),
        detection(60, 120, 110, 300, 500),
    };
    const QJsonArray tracks = buildVibeCutObjectTracks(records, QStringLiteral("bin:1"), QStringLiteral("person"), 0.5, 0.25, 2, 2);
    REQUIRE(tracks.size() == 1);
    const QJsonObject track = tracks.at(0).toObject();
    CHECK(track.value(QStringLiteral("authority")).toString() == QStringLiteral("derived_prediction_track"));
    CHECK(track.value(QStringLiteral("observation_count")).toInt() == 3);
    CHECK(track.value(QStringLiteral("observed_frames")).toArray() == QJsonArray{0, 30, 60});
    REQUIRE(track.value(QStringLiteral("unobserved_intervals")).toArray().size() == 2);
    CHECK(track.value(QStringLiteral("coverage_semantics")).toString() == QStringLiteral("sampled_predictions_only"));
}

TEST_CASE("object tracks never cross source fingerprints", "[vibecut][objects][tracks][provenance]")
{
    const QJsonArray records{
        detection(0, 100, 100, 300, 500, QStringLiteral("person"), QStringLiteral("fp-a")),
        detection(30, 110, 105, 300, 500, QStringLiteral("person"), QStringLiteral("fp-a")),
        detection(60, 120, 110, 300, 500, QStringLiteral("person"), QStringLiteral("fp-b")),
        detection(90, 125, 115, 300, 500, QStringLiteral("person"), QStringLiteral("fp-b")),
    };
    const QJsonArray tracks = buildVibeCutObjectTracks(records, QString(), QString(), 0.5, 0.25, 2, 2);
    REQUIRE(tracks.size() == 2);
    CHECK(tracks.at(0).toObject().value(QStringLiteral("source_fingerprint")).toString() !=
          tracks.at(1).toObject().value(QStringLiteral("source_fingerprint")).toString());
}

TEST_CASE("object tracks separate same-label objects when geometry does not overlap", "[vibecut][objects][tracks][geometry]")
{
    const QJsonArray records{
        detection(0, 50, 100, 200, 400),
        detection(0, 700, 100, 200, 400),
        detection(30, 60, 105, 200, 400),
        detection(30, 690, 105, 200, 400),
    };
    const QJsonArray tracks = buildVibeCutObjectTracks(records, QStringLiteral("bin:1"), QStringLiteral("person"), 0.5, 0.25, 1, 2);
    REQUIRE(tracks.size() == 2);
    CHECK(tracks.at(0).toObject().value(QStringLiteral("observation_count")).toInt() == 2);
    CHECK(tracks.at(1).toObject().value(QStringLiteral("observation_count")).toInt() == 2);
}

TEST_CASE("object tracks split when prediction geometry continuity is lost", "[vibecut][objects][tracks][geometry]")
{
    const QJsonArray records{
        detection(0, 100, 100, 250, 450),
        detection(30, 110, 105, 250, 450),
        detection(60, 700, 100, 250, 450),
        detection(90, 690, 105, 250, 450),
    };
    const QJsonArray tracks = buildVibeCutObjectTracks(records, QStringLiteral("bin:1"), QStringLiteral("person"), 0.5, 0.25, 1, 2);
    REQUIRE(tracks.size() == 2);
    CHECK(tracks.at(0).toObject().value(QStringLiteral("observed_frames")).toArray() == QJsonArray{0, 30});
    CHECK(tracks.at(1).toObject().value(QStringLiteral("observed_frames")).toArray() == QJsonArray{60, 90});
}
