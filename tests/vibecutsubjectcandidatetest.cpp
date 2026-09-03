/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutsubjectcandidates.h"

namespace {
QJsonObject detection(int frame, int x, int y, int width, int height, const QString &label, double score)
{
    return QJsonObject{
        {QStringLiteral("source_id"), QStringLiteral("bin:1")},
        {QStringLiteral("source_fingerprint"), QStringLiteral("fp-a")},
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

TEST_CASE("subject candidates rank transparent visual prominence without identity claims", "[vibecut][subjects][ranking]")
{
    const QJsonArray records{
        detection(0, 250, 150, 500, 700, QStringLiteral("person"), 0.90),
        detection(30, 255, 150, 500, 700, QStringLiteral("person"), 0.91),
        detection(60, 260, 150, 500, 700, QStringLiteral("person"), 0.89),
        detection(0, 20, 20, 120, 120, QStringLiteral("car"), 0.96),
        detection(30, 25, 20, 120, 120, QStringLiteral("car"), 0.95),
        detection(60, 30, 20, 120, 120, QStringLiteral("car"), 0.95),
    };
    const QJsonArray candidates = buildVibeCutSubjectCandidates(records, QStringLiteral("bin:1"), QString(), 0.5, 0.2, 1, 2, 10);
    REQUIRE(candidates.size() == 2);
    const QJsonObject first = candidates.at(0).toObject();
    CHECK(first.value(QStringLiteral("label")).toString() == QStringLiteral("person"));
    CHECK(first.value(QStringLiteral("rank")).toInt() == 1);
    CHECK(first.value(QStringLiteral("authority")).toString() == QStringLiteral("derived_candidate"));
    CHECK(first.value(QStringLiteral("candidate_kind")).toString() == QStringLiteral("editorial_visual_subject"));
    CHECK(first.value(QStringLiteral("prominence_components")).toObject().value(QStringLiteral("weights")).isObject());
    CHECK(first.value(QStringLiteral("note")).toString().contains(QStringLiteral("not object/person identity")));
}

TEST_CASE("subject candidate ranking remains label-filterable and bounded", "[vibecut][subjects][filter]")
{
    const QJsonArray records{
        detection(0, 250, 150, 500, 700, QStringLiteral("person"), 0.90),
        detection(30, 255, 150, 500, 700, QStringLiteral("person"), 0.91),
        detection(0, 300, 300, 300, 300, QStringLiteral("car"), 0.95),
        detection(30, 305, 300, 300, 300, QStringLiteral("car"), 0.95),
    };
    const QJsonArray candidates = buildVibeCutSubjectCandidates(records, QStringLiteral("bin:1"), QStringLiteral("car"), 0.5, 0.2, 1, 2, 1);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates.at(0).toObject().value(QStringLiteral("label")).toString() == QStringLiteral("car"));
}
