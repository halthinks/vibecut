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

VibeCutMediaEvidenceRecord objectDetection(const QString &label, int labelId, int frame, double score)
{
    VibeCutMediaEvidenceRecord record;
    record.kind = QStringLiteral("object_detection_prediction");
    record.startFrame = frame;
    record.endFrame = frame + 1;
    record.text = QStringLiteral("COCO object prediction: %1").arg(label);
    record.confidence = score;
    record.metadata = QJsonObject{
        {QStringLiteral("sample_frame"), frame},
        {QStringLiteral("image_width"), 1920},
        {QStringLiteral("image_height"), 1080},
        {QStringLiteral("bbox_pixels"), QJsonObject{{QStringLiteral("x"), 300}, {QStringLiteral("y"), 100},
                                                     {QStringLiteral("width"), 500}, {QStringLiteral("height"), 800}}},
        {QStringLiteral("label"), label},
        {QStringLiteral("label_id"), labelId},
        {QStringLiteral("model"), QStringLiteral("facebook/detr-resnet-50")},
        {QStringLiteral("model_revision"), QStringLiteral("ebd66332d81f2ee6d9fbfefd0235026b46a381d0")},
        {QStringLiteral("taxonomy"), QStringLiteral("COCO-2017")},
        {QStringLiteral("authority"), QStringLiteral("model_prediction")},
    };
    return record;
}

VibeCutMediaEvidenceRecord actionPrediction(const QString &label, int labelId, int rank, int start, int end, double score)
{
    VibeCutMediaEvidenceRecord record;
    record.kind = QStringLiteral("action_prediction");
    record.startFrame = start;
    record.endFrame = end;
    record.text = QStringLiteral("X-CLIP action prediction: %1").arg(label);
    record.confidence = score;
    QJsonArray frames;
    for (int i = 0; i < 8; ++i) frames.append(start + i * qMax(1, (end - start - 1) / 7));
    record.metadata = QJsonObject{
        {QStringLiteral("label"), label},
        {QStringLiteral("prompt"), QStringLiteral("a video of %1").arg(label)},
        {QStringLiteral("label_id"), labelId},
        {QStringLiteral("rank"), rank},
        {QStringLiteral("window_start_frame"), start},
        {QStringLiteral("window_end_frame"), end},
        {QStringLiteral("observed_frames"), frames},
        {QStringLiteral("model"), QStringLiteral("microsoft/xclip-base-patch32")},
        {QStringLiteral("model_revision"), QStringLiteral("47627d79085e55e641829bd120ac64a3cc3c2238")},
        {QStringLiteral("taxonomy"), QStringLiteral("VibeCutActionSet-v1")},
        {QStringLiteral("score_semantics"), QStringLiteral("softmax_over_fixed_action_set")},
        {QStringLiteral("action_set_sha256"), QStringLiteral("005794f327b4bbf0cea1dd3801009f1c9c51066fec0bb129b7a01b0f8d5520fc")},
        {QStringLiteral("candidate_count"), 47},
        {QStringLiteral("authority"), QStringLiteral("model_prediction")},
    };
    return record;
}
}

TEST_CASE("diarization evidence contract accepts source-bounded anonymous speaker clusters", "[vibecut][extractor-provider][diarization]")
{
    QList<VibeCutMediaEvidenceRecord> records{speakerSegment(QStringLiteral("SPEAKER_00"), 10, 40), speakerSegment(QStringLiteral("SPEAKER_01"), 40, 85)};
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
    CHECK(validateVibeCutExtractorEvidenceContract(QStringLiteral("ocr"), 0, 100, {ocrText(QStringLiteral("SALE"), 20)}, &error));
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
    outsideBox.metadata.insert(QStringLiteral("bbox_pixels"), QJsonObject{{QStringLiteral("x"), 1800}, {QStringLiteral("y"), 1000},
                                                                          {QStringLiteral("width"), 300}, {QStringLiteral("height"), 200}});
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("ocr"), 0, 100, {outsideBox}, &error));
    CHECK(error.contains(QStringLiteral("bbox_pixels")));
    error.clear();
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("ocr"), 0, 100, {ocrText(QStringLiteral("SALE"), 100)}, &error));
    CHECK(error.contains(QStringLiteral("outside"), Qt::CaseInsensitive));
}

TEST_CASE("audio-event evidence contract accepts ranked model predictions with exact window provenance", "[vibecut][extractor-provider][audio-events]")
{
    QString error;
    QList<VibeCutMediaEvidenceRecord> records{audioEvent(QStringLiteral("Speech"), 0, 1, 100, 350, 0.81),
                                               audioEvent(QStringLiteral("Background music"), 267, 2, 100, 350, 0.12)};
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

TEST_CASE("object-detection contract accepts exact sampled-frame model predictions with bounded boxes", "[vibecut][extractor-provider][objects]")
{
    QString error;
    CHECK(validateVibeCutExtractorEvidenceContract(QStringLiteral("objects"), 0, 100,
                                                    {objectDetection(QStringLiteral("person"), 1, 20, 0.92)}, &error));
    CHECK(error.isEmpty());
}

TEST_CASE("object-detection contract rejects fact promotion loose frames bad boxes and missing model revision", "[vibecut][extractor-provider][objects]")
{
    QString error;
    VibeCutMediaEvidenceRecord fact = objectDetection(QStringLiteral("person"), 1, 20, 0.92);
    fact.metadata.insert(QStringLiteral("authority"), QStringLiteral("observation"));
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("objects"), 0, 100, {fact}, &error));
    CHECK(error.contains(QStringLiteral("model_prediction")));

    error.clear();
    VibeCutMediaEvidenceRecord loose = objectDetection(QStringLiteral("person"), 1, 20, 0.92);
    loose.endFrame = 23;
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("objects"), 0, 100, {loose}, &error));
    CHECK(error.contains(QStringLiteral("one sampled"), Qt::CaseInsensitive));

    error.clear();
    VibeCutMediaEvidenceRecord badBox = objectDetection(QStringLiteral("person"), 1, 20, 0.92);
    badBox.metadata.insert(QStringLiteral("bbox_pixels"), QJsonObject{{QStringLiteral("x"), 1800}, {QStringLiteral("y"), 900},
                                                                       {QStringLiteral("width"), 300}, {QStringLiteral("height"), 300}});
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("objects"), 0, 100, {badBox}, &error));
    CHECK(error.contains(QStringLiteral("bbox_pixels")));

    error.clear();
    VibeCutMediaEvidenceRecord noRevision = objectDetection(QStringLiteral("person"), 1, 20, 0.92);
    noRevision.metadata.remove(QStringLiteral("model_revision"));
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("objects"), 0, 100, {noRevision}, &error));
    CHECK(error.contains(QStringLiteral("model_revision")));
}

TEST_CASE("action contract accepts ranked X-CLIP predictions with exact eight-frame and calibration provenance", "[vibecut][extractor-provider][actions]")
{
    QString error;
    CHECK(validateVibeCutExtractorEvidenceContract(QStringLiteral("actions"), 0, 500,
                                                    {actionPrediction(QStringLiteral("walking"), 2, 1, 100, 181, 0.72)}, &error));
    CHECK(error.isEmpty());
}

TEST_CASE("action contract rejects fact promotion malformed observed frames and missing model provenance", "[vibecut][extractor-provider][actions]")
{
    QString error;
    VibeCutMediaEvidenceRecord fact = actionPrediction(QStringLiteral("walking"), 2, 1, 100, 181, 0.72);
    fact.metadata.insert(QStringLiteral("authority"), QStringLiteral("observation"));
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("actions"), 0, 500, {fact}, &error));
    CHECK(error.contains(QStringLiteral("model_prediction")));

    error.clear();
    VibeCutMediaEvidenceRecord badFrames = actionPrediction(QStringLiteral("walking"), 2, 1, 100, 181, 0.72);
    badFrames.metadata.insert(QStringLiteral("observed_frames"), QJsonArray{100, 110, 120, 130, 140, 150, 160});
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("actions"), 0, 500, {badFrames}, &error));
    CHECK(error.contains(QStringLiteral("8 observed"), Qt::CaseInsensitive));

    error.clear();
    VibeCutMediaEvidenceRecord outsideFrame = actionPrediction(QStringLiteral("walking"), 2, 1, 100, 181, 0.72);
    outsideFrame.metadata.insert(QStringLiteral("observed_frames"), QJsonArray{100, 111, 122, 133, 144, 155, 166, 181});
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("actions"), 0, 500, {outsideFrame}, &error));
    CHECK(error.contains(QStringLiteral("inside"), Qt::CaseInsensitive));

    error.clear();
    VibeCutMediaEvidenceRecord noRevision = actionPrediction(QStringLiteral("walking"), 2, 1, 100, 181, 0.72);
    noRevision.metadata.remove(QStringLiteral("model_revision"));
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("actions"), 0, 500, {noRevision}, &error));
    CHECK(error.contains(QStringLiteral("model_revision")));
}

TEST_CASE("action contract requires fixed-set score semantics and candidate-set identity", "[vibecut][extractor-provider][actions][calibration]")
{
    QString error;
    VibeCutMediaEvidenceRecord missingSemantics = actionPrediction(QStringLiteral("walking"), 2, 1, 100, 181, 0.72);
    missingSemantics.metadata.remove(QStringLiteral("score_semantics"));
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("actions"), 0, 500, {missingSemantics}, &error));
    CHECK(error.contains(QStringLiteral("score_semantics")));

    error.clear();
    VibeCutMediaEvidenceRecord badHash = actionPrediction(QStringLiteral("walking"), 2, 1, 100, 181, 0.72);
    badHash.metadata.insert(QStringLiteral("action_set_sha256"), QStringLiteral("NOT-A-SHA"));
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("actions"), 0, 500, {badHash}, &error));
    CHECK(error.contains(QStringLiteral("action_set_sha256")));

    error.clear();
    VibeCutMediaEvidenceRecord badCount = actionPrediction(QStringLiteral("walking"), 2, 1, 100, 181, 0.72);
    badCount.metadata.insert(QStringLiteral("candidate_count"), 2);
    badCount.metadata.insert(QStringLiteral("label_id"), 2);
    CHECK_FALSE(validateVibeCutExtractorEvidenceContract(QStringLiteral("actions"), 0, 500, {badCount}, &error));
    CHECK(error.contains(QStringLiteral("candidate set"), Qt::CaseInsensitive));
}
