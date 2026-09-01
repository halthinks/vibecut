/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutmediaindex.h"

TEST_CASE("media index ranks exact transcript phrases above token-only matches", "[vibecut][media]")
{
    VibeCutMediaIndex index;
    VibeCutMediaDocument a;
    a.id = QStringLiteral("a"); a.kind = QStringLiteral("transcript"); a.text = QStringLiteral("the red gearbox prototype failed"); a.startFrame = 20;
    VibeCutMediaDocument b;
    b.id = QStringLiteral("b"); b.kind = QStringLiteral("clip"); b.text = QStringLiteral("gearbox camera red"); b.startFrame = 10;
    index.add(a); index.add(b);
    const QList<VibeCutMediaSearchHit> hits = index.search(QStringLiteral("red gearbox"));
    REQUIRE(hits.size() == 2);
    CHECK(hits.first().document.id == QStringLiteral("a"));
    CHECK(hits.first().score > hits.last().score);
}

TEST_CASE("media index searches cross-modal evidence identity without requiring text", "[vibecut][media]")
{
    VibeCutMediaIndex index;
    VibeCutMediaDocument speaker;
    speaker.id = QStringLiteral("speaker-turn");
    speaker.kind = QStringLiteral("speaker_turn");
    speaker.startFrame = 100;
    speaker.endFrame = 140;
    speaker.metadata = QJsonObject{{QStringLiteral("evidence_origin"), QStringLiteral("extractor")},
                                   {QStringLiteral("modality"), QStringLiteral("audio")},
                                   {QStringLiteral("speaker_id"), QStringLiteral("speaker:0")},
                                   {QStringLiteral("speaker_name"), QStringLiteral("Alice")},
                                   {QStringLiteral("confidence"), 0.95}};
    index.add(speaker);

    VibeCutMediaDocument object;
    object.id = QStringLiteral("visual-object");
    object.kind = QStringLiteral("object");
    object.metadata = QJsonObject{{QStringLiteral("evidence_origin"), QStringLiteral("extractor")},
                                  {QStringLiteral("modality"), QStringLiteral("visual")},
                                  {QStringLiteral("label"), QStringLiteral("red gearbox")},
                                  {QStringLiteral("subject_id"), QStringLiteral("subject:42")}};
    index.add(object);

    const QList<VibeCutMediaSearchHit> speakerHits = index.search(QStringLiteral("Alice"));
    REQUIRE(speakerHits.size() == 1);
    CHECK(speakerHits.first().document.id == QStringLiteral("speaker-turn"));

    const QList<VibeCutMediaSearchHit> objectHits = index.search(QStringLiteral("red gearbox"));
    REQUIRE(objectHits.size() == 1);
    CHECK(objectHits.first().document.id == QStringLiteral("visual-object"));
}
