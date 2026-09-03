/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutextractorevidencecontract.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>
#include <QtGlobal>

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool withinRequestedRange(const VibeCutMediaEvidenceRecord &record, int startFrame, int endFrame)
{
    if (record.startFrame < 0 && record.endFrame < 0) return true;
    if (record.startFrame < 0 || record.endFrame < 0) return false;
    return record.startFrame >= startFrame && record.endFrame <= endFrame && record.endFrame >= record.startFrame;
}

bool validatePixelBox(const QJsonObject &metadata, const QString &prefix, QString *error)
{
    const int imageWidth = metadata.value(QStringLiteral("image_width")).toInt(-1);
    const int imageHeight = metadata.value(QStringLiteral("image_height")).toInt(-1);
    if (imageWidth <= 0 || imageHeight <= 0) {
        return fail(error, QStringLiteral("%1 metadata requires positive image_width and image_height.").arg(prefix));
    }
    const QJsonObject box = metadata.value(QStringLiteral("bbox_pixels")).toObject();
    const int x = box.value(QStringLiteral("x")).toInt(-1);
    const int y = box.value(QStringLiteral("y")).toInt(-1);
    const int width = box.value(QStringLiteral("width")).toInt(-1);
    const int height = box.value(QStringLiteral("height")).toInt(-1);
    if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
        static_cast<qint64>(x) + static_cast<qint64>(width) > imageWidth ||
        static_cast<qint64>(y) + static_cast<qint64>(height) > imageHeight) {
        return fail(error, QStringLiteral("%1 bbox_pixels must be a positive rectangle fully contained by the sampled image.").arg(prefix));
    }
    return true;
}

bool validateOcrRecord(const VibeCutMediaEvidenceRecord &record, QString *error)
{
    if (record.kind != QLatin1String("ocr_text")) {
        return fail(error, QStringLiteral("OCR providers may persist only 'ocr_text' evidence records."));
    }
    if (record.startFrame < 0 || record.endFrame != record.startFrame + 1) {
        return fail(error, QStringLiteral("OCR text observations must identify exactly one sampled source frame as [frame, frame+1)."));
    }
    const QString text = record.text.trimmed();
    if (text.isEmpty() || text.size() > 4096) {
        return fail(error, QStringLiteral("OCR text must contain 1 to 4096 non-whitespace characters."));
    }
    if (record.confidence < 0.0 || record.confidence > 1.0) {
        return fail(error, QStringLiteral("OCR text requires normalized confidence between 0 and 1."));
    }
    const int sampleFrame = record.metadata.value(QStringLiteral("sample_frame")).toInt(-1);
    if (sampleFrame != record.startFrame) {
        return fail(error, QStringLiteral("OCR metadata.sample_frame must equal the evidence start_frame."));
    }
    if (!validatePixelBox(record.metadata, QStringLiteral("OCR"), error)) return false;
    const QString language = record.metadata.value(QStringLiteral("language")).toString().trimmed();
    if (language.isEmpty() || language.size() > 128) {
        return fail(error, QStringLiteral("OCR metadata.language must contain 1 to 128 characters."));
    }
    const QString engine = record.metadata.value(QStringLiteral("engine")).toString().trimmed();
    if (engine.isEmpty() || engine.size() > 128) {
        return fail(error, QStringLiteral("OCR metadata.engine must contain 1 to 128 characters."));
    }
    return true;
}

bool validateAudioEventRecord(const VibeCutMediaEvidenceRecord &record, QString *error)
{
    if (record.kind != QLatin1String("audio_event_prediction")) {
        return fail(error, QStringLiteral("Audio-event providers may persist only 'audio_event_prediction' evidence records."));
    }
    if (record.startFrame < 0 || record.endFrame <= record.startFrame) {
        return fail(error, QStringLiteral("Audio-event predictions require an exact non-empty source-frame window."));
    }
    if (record.confidence < 0.0 || record.confidence > 1.0) {
        return fail(error, QStringLiteral("Audio-event predictions require a normalized score between 0 and 1."));
    }
    const QString label = record.metadata.value(QStringLiteral("label")).toString().trimmed();
    if (label.isEmpty() || label.size() > 256) {
        return fail(error, QStringLiteral("Audio-event metadata.label must contain 1 to 256 characters."));
    }
    const QJsonValue labelIdValue = record.metadata.value(QStringLiteral("label_id"));
    if (!labelIdValue.isDouble()) {
        return fail(error, QStringLiteral("Audio-event metadata.label_id must be a non-negative integer."));
    }
    const int labelId = labelIdValue.toInt(-1);
    if (labelId < 0 || static_cast<double>(labelId) != labelIdValue.toDouble()) {
        return fail(error, QStringLiteral("Audio-event metadata.label_id must be a non-negative integer."));
    }
    const QJsonValue rankValue = record.metadata.value(QStringLiteral("rank"));
    if (!rankValue.isDouble()) {
        return fail(error, QStringLiteral("Audio-event metadata.rank must be a positive integer."));
    }
    const int rank = rankValue.toInt(-1);
    if (rank < 1 || rank > 100 || static_cast<double>(rank) != rankValue.toDouble()) {
        return fail(error, QStringLiteral("Audio-event metadata.rank must be an integer from 1 to 100."));
    }
    if (record.metadata.value(QStringLiteral("window_start_frame")).toInt(-1) != record.startFrame ||
        record.metadata.value(QStringLiteral("window_end_frame")).toInt(-1) != record.endFrame) {
        return fail(error, QStringLiteral("Audio-event metadata window bounds must exactly match the evidence frame range."));
    }
    const QString model = record.metadata.value(QStringLiteral("model")).toString().trimmed();
    if (model.isEmpty() || model.size() > 256) {
        return fail(error, QStringLiteral("Audio-event metadata.model must contain 1 to 256 characters."));
    }
    const QString taxonomy = record.metadata.value(QStringLiteral("taxonomy")).toString().trimmed();
    if (taxonomy.isEmpty() || taxonomy.size() > 128) {
        return fail(error, QStringLiteral("Audio-event metadata.taxonomy must contain 1 to 128 characters."));
    }
    if (record.metadata.value(QStringLiteral("authority")).toString() != QLatin1String("model_prediction")) {
        return fail(error, QStringLiteral("Audio-event evidence must declare authority='model_prediction'."));
    }
    if (record.text.trimmed().isEmpty() || record.text.size() > 1024) {
        return fail(error, QStringLiteral("Audio-event prediction text must contain 1 to 1024 characters."));
    }
    return true;
}

bool validateObjectDetectionRecord(const VibeCutMediaEvidenceRecord &record, QString *error)
{
    if (record.kind != QLatin1String("object_detection_prediction")) {
        return fail(error, QStringLiteral("Object-detection providers may persist only 'object_detection_prediction' evidence records."));
    }
    if (record.startFrame < 0 || record.endFrame != record.startFrame + 1) {
        return fail(error, QStringLiteral("Object detections must identify exactly one sampled source frame as [frame, frame+1)."));
    }
    if (record.confidence < 0.0 || record.confidence > 1.0) {
        return fail(error, QStringLiteral("Object detections require a normalized model score between 0 and 1."));
    }
    if (record.metadata.value(QStringLiteral("sample_frame")).toInt(-1) != record.startFrame) {
        return fail(error, QStringLiteral("Object-detection metadata.sample_frame must equal evidence start_frame."));
    }
    if (!validatePixelBox(record.metadata, QStringLiteral("Object-detection"), error)) return false;
    const QString label = record.metadata.value(QStringLiteral("label")).toString().trimmed();
    if (label.isEmpty() || label.size() > 256) {
        return fail(error, QStringLiteral("Object-detection metadata.label must contain 1 to 256 characters."));
    }
    const QJsonValue labelIdValue = record.metadata.value(QStringLiteral("label_id"));
    const int labelId = labelIdValue.toInt(-1);
    if (!labelIdValue.isDouble() || labelId < 0 || static_cast<double>(labelId) != labelIdValue.toDouble()) {
        return fail(error, QStringLiteral("Object-detection metadata.label_id must be a non-negative integer."));
    }
    const QString model = record.metadata.value(QStringLiteral("model")).toString().trimmed();
    const QString modelRevision = record.metadata.value(QStringLiteral("model_revision")).toString().trimmed();
    const QString taxonomy = record.metadata.value(QStringLiteral("taxonomy")).toString().trimmed();
    if (model.isEmpty() || model.size() > 256 || modelRevision.isEmpty() || modelRevision.size() > 128 ||
        taxonomy.isEmpty() || taxonomy.size() > 128) {
        return fail(error, QStringLiteral("Object-detection metadata requires bounded model, model_revision and taxonomy provenance."));
    }
    if (record.metadata.value(QStringLiteral("authority")).toString() != QLatin1String("model_prediction")) {
        return fail(error, QStringLiteral("Object-detection evidence must declare authority='model_prediction'."));
    }
    if (record.text.trimmed().isEmpty() || record.text.size() > 1024) {
        return fail(error, QStringLiteral("Object-detection prediction text must contain 1 to 1024 characters."));
    }
    return true;
}

bool validateActionRecord(const VibeCutMediaEvidenceRecord &record, QString *error)
{
    if (record.kind != QLatin1String("action_prediction")) {
        return fail(error, QStringLiteral("Action providers may persist only 'action_prediction' evidence records."));
    }
    if (record.startFrame < 0 || record.endFrame <= record.startFrame) {
        return fail(error, QStringLiteral("Action predictions require an exact non-empty source-frame window."));
    }
    if (record.confidence < 0.0 || record.confidence > 1.0) {
        return fail(error, QStringLiteral("Action predictions require a normalized model score between 0 and 1."));
    }
    const QString label = record.metadata.value(QStringLiteral("label")).toString().trimmed();
    const QString prompt = record.metadata.value(QStringLiteral("prompt")).toString().trimmed();
    if (label.isEmpty() || label.size() > 256 || prompt.isEmpty() || prompt.size() > 512) {
        return fail(error, QStringLiteral("Action metadata requires bounded non-empty label and prompt fields."));
    }
    const QJsonValue labelIdValue = record.metadata.value(QStringLiteral("label_id"));
    const int labelId = labelIdValue.toInt(-1);
    if (!labelIdValue.isDouble() || labelId < 0 || static_cast<double>(labelId) != labelIdValue.toDouble()) {
        return fail(error, QStringLiteral("Action metadata.label_id must be a non-negative integer."));
    }
    const QJsonValue rankValue = record.metadata.value(QStringLiteral("rank"));
    const int rank = rankValue.toInt(-1);
    if (!rankValue.isDouble() || rank < 1 || rank > 100 || static_cast<double>(rank) != rankValue.toDouble()) {
        return fail(error, QStringLiteral("Action metadata.rank must be an integer from 1 to 100."));
    }
    if (record.metadata.value(QStringLiteral("window_start_frame")).toInt(-1) != record.startFrame ||
        record.metadata.value(QStringLiteral("window_end_frame")).toInt(-1) != record.endFrame) {
        return fail(error, QStringLiteral("Action metadata window bounds must exactly match the evidence frame range."));
    }
    const QJsonArray observedFrames = record.metadata.value(QStringLiteral("observed_frames")).toArray();
    if (observedFrames.size() != 8) {
        return fail(error, QStringLiteral("Action predictions must retain exactly 8 observed source frames for the pinned X-CLIP model."));
    }
    int previous = -1;
    for (const QJsonValue &value : observedFrames) {
        if (!value.isDouble()) return fail(error, QStringLiteral("Action observed_frames must contain integer frame indices."));
        const int frame = value.toInt(-1);
        if (frame < record.startFrame || frame >= record.endFrame || frame <= previous || static_cast<double>(frame) != value.toDouble()) {
            return fail(error, QStringLiteral("Action observed_frames must be strictly increasing integer frames inside the prediction window."));
        }
        previous = frame;
    }
    const QString model = record.metadata.value(QStringLiteral("model")).toString().trimmed();
    const QString modelRevision = record.metadata.value(QStringLiteral("model_revision")).toString().trimmed();
    const QString taxonomy = record.metadata.value(QStringLiteral("taxonomy")).toString().trimmed();
    if (model.isEmpty() || model.size() > 256 || modelRevision.isEmpty() || modelRevision.size() > 128 ||
        taxonomy.isEmpty() || taxonomy.size() > 128) {
        return fail(error, QStringLiteral("Action metadata requires bounded model, model_revision and taxonomy provenance."));
    }
    const QString scoreSemantics = record.metadata.value(QStringLiteral("score_semantics")).toString().trimmed();
    if (scoreSemantics != QLatin1String("softmax_over_fixed_action_set")) {
        return fail(error, QStringLiteral("Action evidence must declare score_semantics='softmax_over_fixed_action_set'."));
    }
    const QString actionSetHash = record.metadata.value(QStringLiteral("action_set_sha256")).toString().trimmed();
    if (actionSetHash.size() != 64) {
        return fail(error, QStringLiteral("Action metadata.action_set_sha256 must be a 64-character lowercase SHA-256 digest."));
    }
    for (const QChar ch : actionSetHash) {
        if (!(ch.isDigit() || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')))) {
            return fail(error, QStringLiteral("Action metadata.action_set_sha256 must be a 64-character lowercase SHA-256 digest."));
        }
    }
    const QJsonValue candidateCountValue = record.metadata.value(QStringLiteral("candidate_count"));
    const int candidateCount = candidateCountValue.toInt(-1);
    if (!candidateCountValue.isDouble() || candidateCount < 1 || candidateCount > 10000 ||
        static_cast<double>(candidateCount) != candidateCountValue.toDouble()) {
        return fail(error, QStringLiteral("Action metadata.candidate_count must be a positive integer up to 10000."));
    }
    if (labelId >= candidateCount) {
        return fail(error, QStringLiteral("Action metadata.label_id must be within the declared fixed candidate set."));
    }
    if (record.metadata.value(QStringLiteral("authority")).toString() != QLatin1String("model_prediction")) {
        return fail(error, QStringLiteral("Action evidence must declare authority='model_prediction'."));
    }
    if (record.text.trimmed().isEmpty() || record.text.size() > 1024) {
        return fail(error, QStringLiteral("Action prediction text must contain 1 to 1024 characters."));
    }
    return true;
}
}

bool validateVibeCutExtractorEvidenceContract(const QString &capability,
                                              int requestedStartFrame,
                                              int requestedEndFrame,
                                              const QList<VibeCutMediaEvidenceRecord> &records,
                                              QString *error)
{
    if (error) error->clear();
    const QString normalizedCapability = capability.trimmed().toLower();
    if (normalizedCapability.isEmpty()) return fail(error, QStringLiteral("Extractor evidence contract requires a capability."));
    if (requestedStartFrame < 0 || requestedEndFrame < requestedStartFrame) {
        return fail(error, QStringLiteral("Extractor evidence contract received invalid authoritative request bounds."));
    }

    for (const VibeCutMediaEvidenceRecord &record : records) {
        if (!withinRequestedRange(record, requestedStartFrame, requestedEndFrame)) {
            return fail(error, QStringLiteral("Provider evidence range [%1,%2) falls outside authoritative request bounds [%3,%4).")
                                   .arg(record.startFrame).arg(record.endFrame).arg(requestedStartFrame).arg(requestedEndFrame));
        }

        if (normalizedCapability == QLatin1String("ocr")) {
            if (!validateOcrRecord(record, error)) return false;
            continue;
        }
        if (normalizedCapability == QLatin1String("audio_events")) {
            if (!validateAudioEventRecord(record, error)) return false;
            continue;
        }
        if (normalizedCapability == QLatin1String("objects")) {
            if (!validateObjectDetectionRecord(record, error)) return false;
            continue;
        }
        if (normalizedCapability == QLatin1String("actions")) {
            if (!validateActionRecord(record, error)) return false;
            continue;
        }
        if (normalizedCapability != QLatin1String("diarization")) continue;

        if (record.kind != QLatin1String("speaker_segment")) {
            return fail(error, QStringLiteral("Diarization providers may persist only 'speaker_segment' evidence records."));
        }
        if (record.startFrame < 0 || record.endFrame <= record.startFrame) {
            return fail(error, QStringLiteral("Diarization speaker segments require an exact non-empty frame range."));
        }
        const QString clusterId = record.metadata.value(QStringLiteral("speaker_cluster_id")).toString().trimmed();
        if (clusterId.isEmpty()) {
            return fail(error, QStringLiteral("Diarization speaker segments require metadata.speaker_cluster_id."));
        }
        if (clusterId.size() > 128) {
            return fail(error, QStringLiteral("Diarization speaker_cluster_id may not exceed 128 characters."));
        }
        if (record.metadata.contains(QStringLiteral("overlap")) && !record.metadata.value(QStringLiteral("overlap")).isBool()) {
            return fail(error, QStringLiteral("Diarization metadata.overlap must be boolean when present."));
        }
        if (record.metadata.contains(QStringLiteral("channel"))) {
            const QJsonValue channel = record.metadata.value(QStringLiteral("channel"));
            if (!channel.isDouble() || channel.toInt(-1) < 0) {
                return fail(error, QStringLiteral("Diarization metadata.channel must be a non-negative integer when present."));
            }
        }

        const QStringList forbiddenIdentityKeys{
            QStringLiteral("speaker_name"),
            QStringLiteral("display_name"),
            QStringLiteral("person_id"),
            QStringLiteral("identity_id"),
            QStringLiteral("speaker_entity_id"),
        };
        for (const QString &key : forbiddenIdentityKeys) {
            if (record.metadata.contains(key)) {
                return fail(error, QStringLiteral("Diarization providers may cluster speakers but may not assert identity metadata '%1'.").arg(key));
            }
        }
    }
    return true;
}
