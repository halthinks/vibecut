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

VibeCutMediaEvidenceRecord ocrText(const QString &text, int frame)
{
    VibeCutMediaEvidenceRecord record;
    record.kind = QStringLiteral("ocr_text");
    record.startFrame = frame;
    record.endFrame = frame + 1;
    record.text = text;
    record.confidence = 0.93;
    record.metadata = QJsonObject{
        {QStringLiteral("sample_frame"), frame},
        {QStringLiteral("image_width"), 1920},
        {QStringLiteral("image_height"), 1080},
        {QStringLiteral("bbox_pixels"), QJsonObject{{QStringLiteral("x"), 100}, {QStringLiteral("y"), 200},
                                                     {QStringLiteral("width"), 600}, {QStringLiteral("height"), 80}}},
        {QStringLiteral("language"), QStringLiteral("eng")},
        {QStringLiteral("engine"), QStringLiteral("tesseract-5")},
    };
    return record;
}

VibeCutMediaEvidenceRecord audioEvent(const QString &label, int labelId, int rank, int start, int end, double score)
{
    VibeCutMediaEvidenceRecord record;
    record.kind = QStringLiteral("audio_event_prediction");
    record.startFrame = start;
    record.endFrame = end;
    record.text = QStringLiteral("AudioSet prediction: %1").arg(label);
    record.confidence = score;
    record.metadata = QJsonObject{
        {QStringLiteral("label"), label},
        {QStringLiteral("label_id"), labelId},
        {QStringLiteral("rank"), rank},
        {QStringLiteral("window_start_frame"), start},
        {QStringLiteral("window_end_frame"), end},
        {QStringLiteral("model"), QStringLiteral("MIT/ast-finetuned-audioset-10-10-0.4593")},
        {QStringLiteral("taxonomy"), QStringLiteral("AudioSet")},
        {QStringLiteral("authority"), QStringLiteral("model_prediction")},
    };
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

TEST_CASE("OCR evidence contract accepts exact-frame text with bounded geometry and provenance", "[vibecut][extractor-provider][ocr]")
{
    QString error;
    const VibeCutMediaEvidenceRecord record = ocrText(QStringLiteral("SALE"), 20);
    CHECK(validateVibeCutExtractorEvidenceContract(QStringLiteral("ocr"), 0, 100, {record}, &error));
    CHECK(error.isEmpty());
}

TEST_CASE("OCR evidence contract rejects loose ranges missing confidence and invalid geometry", "[vibecut][extractor-provider][ocr]")
{
    QString error;

    VibeCutMediaEvidenceRecord loose = ocrText(QStringLiteral("SALE"), 20);
    loose.endFrame = 25;
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("ocr"), 0, 100, {loose}, &error));
    CHECK(error.contains(QStringLiteral("one sampled"), Qt::CaseInsensitive));

    error.clear();
    VibeCutMediaEvidenceRecord unknownConfidence = ocrText(QStringLiteral("SALE"), 20);
    unknownConfidence.confidence = -1.0;
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("ocr"), 0, 100, {unknownConfidence}, &error));
    CHECK(error.contains(QStringLiteral("confidence"), Qt::CaseInsensitive));

    error.clear();
    VibeCutMediaEvidenceRecord outsideBox = ocrText(QStringLiteral("SALE"), 20);
    outsideBox.metadata.insert(QStringLiteral("bbox_pixels"),
                               QJsonObject{{QStringLiteral("x"), 1800}, {QStringLiteral("y"), 1000},
                                           {QStringLiteral("width"), 300}, {QStringLiteral("height"), 200}});
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("ocr"), 0, 100, {outsideBox}, &error));
    CHECK(error.contains(QStringLiteral("bbox_pixels")));

    error.clear();
    VibeCutMediaEvidenceRecord outsideRange = ocrText(QStringLiteral("SALE"), 100);
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("ocr"), 0, 100, {outsideRange}, &error));
    CHECK(error.contains(QStringLiteral("outside"), Qt::CaseInsensitive));
}

TEST_CASE("audio-event evidence contract accepts ranked model predictions with exact window provenance", "[vibecut][extractor-provider][audio-events]")
{
    QString error;
    QList<VibeCutMediaEvidenceRecord> records{
        audioEvent(QStringLiteral("Speech"), 0, 1, 100, 350, 0.81),
        audioEvent(QStringLiteral("Background music"), 267, 2, 100, 350, 0.12),
    };
    CHECK(validateVibeCutExtractorEvidenceContract(QStringLiteral("audio_events"), 0, 500, records, &error));
    CHECK(error.isEmpty());
}

TEST_CASE("audio-event evidence contract rejects fact promotion malformed ranks and mismatched windows", "[vibecut][extractor-provider][audio-events]")
{
    QString error;

    VibeCutMediaEvidenceRecord fact = audioEvent(QStringLiteral("Speech"), 0, 1, 100, 350, 0.81);
    fact.metadata.insert(QStringLiteral("authority"), QStringLiteral("observation"));
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("audio_events"), 0, 500, {fact}, &error));
    CHECK(error.contains(QStringLiteral("model_prediction")));

    error.clear();
    VibeCutMediaEvidenceRecord badRank = audioEvent(QStringLiteral("Speech"), 0, 0, 100, 350, 0.81);
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("audio_events"), 0, 500, {badRank}, &error));
    CHECK(error.contains(QStringLiteral("rank")));

    error.clear();
    VibeCutMediaEvidenceRecord wrongWindow = audioEvent(QStringLiteral("Speech"), 0, 1, 100, 350, 0.81);
    wrongWindow.metadata.insert(QStringLiteral("window_end_frame"), 351);
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("audio_events"), 0, 500, {wrongWindow}, &error));
    CHECK(error.contains(QStringLiteral("window"), Qt::CaseInsensitive));

    error.clear();
    VibeCutMediaEvidenceRecord wrongKind = audioEvent(QStringLiteral("Speech"), 0, 1, 100, 350, 0.81);
    wrongKind.kind = QStringLiteral("speech");
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("audio_events"), 0, 500, {wrongKind}, &error));
    CHECK(error.contains(QStringLiteral("audio_event_prediction")));
}