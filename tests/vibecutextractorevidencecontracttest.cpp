/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutextractorevidencecontract.h"

namespace {
VibeCutMediaEvidenceRecord speakerSegment(const QString &cluster, int start, int end)
{
    VibeCutMediaEvidenceRecord record;
    record.kind = QStringLiteral("speaker_segment");
    record.startFrame = start;
    record.endFrame = end;
    record.confidence = 0.9;
    record.metadata = QJsonObject{{QStringLiteral("speaker_cluster_id"), cluster}};
    return record;
}
}

TEST_CASE("diarization evidence contract accepts source-bounded anonymous speaker clusters", "[vibecut][extractor-provider][diarization]")
{
    QList<VibeCutMediaEvidenceRecord> records{
        speakerSegment(QStringLiteral("SPEAKER_00"), 10, 40),
        speakerSegment(QStringLiteral("SPEAKER_01"), 40, 85),
    };
    records[1].metadata.insert(QStringLiteral("overlap"), false);
    records[1].metadata.insert(QStringLiteral("channel"), 0);

    QString error;
    CHECK(validateVibeCutExtractorEvidenceContract(QStringLiteral("diarization"), 0, 100, records, &error));
    CHECK(error.isEmpty());
}

TEST_CASE("diarization evidence contract rejects identity assertions from providers", "[vibecut][extractor-provider][diarization]")
{
    VibeCutMediaEvidenceRecord record = speakerSegment(QStringLiteral("SPEAKER_00"), 10, 40);
    record.metadata.insert(QStringLiteral("display_name"), QStringLiteral("Alice"));

    QString error;
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("diarization"), 0, 100, {record}, &error));
    CHECK(error.contains(QStringLiteral("identity"), Qt::CaseInsensitive));
}

TEST_CASE("diarization evidence contract rejects malformed or out-of-scope speaker segments", "[vibecut][extractor-provider][diarization]")
{
    QString error;

    VibeCutMediaEvidenceRecord wrongKind = speakerSegment(QStringLiteral("SPEAKER_00"), 10, 40);
    wrongKind.kind = QStringLiteral("transcript");
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("diarization"), 0, 100, {wrongKind}, &error));
    CHECK(error.contains(QStringLiteral("speaker_segment")));

    error.clear();
    VibeCutMediaEvidenceRecord missingCluster = speakerSegment(QString(), 10, 40);
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("diarization"), 0, 100, {missingCluster}, &error));
    CHECK(error.contains(QStringLiteral("speaker_cluster_id")));

    error.clear();
    VibeCutMediaEvidenceRecord outside = speakerSegment(QStringLiteral("SPEAKER_00"), 90, 110);
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("diarization"), 0, 100, {outside}, &error));
    CHECK(error.contains(QStringLiteral("outside"), Qt::CaseInsensitive));
}

TEST_CASE("non-diarization providers retain generic evidence flexibility within authoritative bounds", "[vibecut][extractor-provider]")
{
    VibeCutMediaEvidenceRecord ocr;
    ocr.kind = QStringLiteral("ocr_text");
    ocr.startFrame = 20;
    ocr.endFrame = 21;
    ocr.text = QStringLiteral("SALE");

    QString error;
    CHECK(validateVibeCutExtractorEvidenceContract(QStringLiteral("ocr"), 0, 100, {ocr}, &error));
    CHECK(error.isEmpty());
}
