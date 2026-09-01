/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecutmediaindex.h"

#include "bin/model/subtitlemodel.hpp"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecutmediaevidence.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>

QJsonObject VibeCutMediaDocument::toJson() const
{
    return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("kind"), kind}, {QStringLiteral("text"), text},
                       {QStringLiteral("start_frame"), startFrame}, {QStringLiteral("end_frame"), endFrame},
                       {QStringLiteral("metadata"), metadata}};
}

QJsonObject VibeCutMediaSearchHit::toJson() const
{
    QJsonObject object = document.toJson();
    object.insert(QStringLiteral("score"), score);
    return object;
}

void VibeCutMediaIndex::clear() { m_documents.clear(); }
void VibeCutMediaIndex::add(const VibeCutMediaDocument &document) { if (!document.text.trimmed().isEmpty()) m_documents.append(document); }
int VibeCutMediaIndex::size() const { return m_documents.size(); }

QList<VibeCutMediaSearchHit> VibeCutMediaIndex::search(const QString &query, int limit) const
{
    const QString needle = query.trimmed();
    QList<VibeCutMediaSearchHit> hits;
    if (needle.isEmpty()) return hits;
    const QStringList tokens = needle.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const VibeCutMediaDocument &document : m_documents) {
        int score = 0;
        if (document.text.contains(needle, Qt::CaseInsensitive)) score += 1000;
        if (document.kind.contains(needle, Qt::CaseInsensitive)) score += 700;

        const QString extractorId = document.metadata.value(QStringLiteral("extractor_id")).toString();
        const QString evidenceOrigin = document.metadata.value(QStringLiteral("evidence_origin")).toString();
        for (const QString &token : tokens) {
            if (token.size() <= 1) continue;
            if (document.text.contains(token, Qt::CaseInsensitive)) score += 25;
            if (document.kind.contains(token, Qt::CaseInsensitive)) score += 80;
            if (extractorId.contains(token, Qt::CaseInsensitive)) score += 40;
        }

        if (score > 0 && evidenceOrigin == QLatin1String("extractor")) {
            score += 10; // Prefer explicit derived evidence over incidental filename matches at equal textual relevance.
            const double confidence = document.metadata.value(QStringLiteral("confidence")).toDouble(-1.0);
            if (confidence >= 0.0) score += qBound(0, qRound(confidence * 20.0), 20);
        }

        if (score > 0) {
            VibeCutMediaSearchHit hit;
            hit.document = document;
            hit.score = score;
            hits.append(hit);
        }
    }
    std::sort(hits.begin(), hits.end(), [](const VibeCutMediaSearchHit &a, const VibeCutMediaSearchHit &b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.document.startFrame != b.document.startFrame) return a.document.startFrame < b.document.startFrame;
        return a.document.id < b.document.id;
    });
    while (hits.size() > qBound(1, limit, 100)) hits.removeLast();
    return hits;
}

bool VibeCutMediaIndex::rebuildFromCurrentProject(QString *error)
{
    if (error) error->clear();
    clear();
    if (!pCore || !pCore->window()) {
        if (error) *error = QStringLiteral("No Kdenlive window is available.");
        return false;
    }
    TimelineWidget *timeline = pCore->window()->getCurrentTimeline();
    const std::shared_ptr<TimelineItemModel> model = timeline ? timeline->model() : nullptr;
    if (!model) {
        if (error) *error = QStringLiteral("No timeline is open.");
        return false;
    }

    for (int trackId : model->getAllTracksIds()) {
        for (int clipId : model->getItemsInRange(trackId, 0, -1, false)) {
            if (!model->isClip(clipId)) continue;
            VibeCutMediaDocument document;
            document.id = QStringLiteral("clip:%1").arg(clipId);
            document.kind = QStringLiteral("clip");
            document.text = model->getClipName(clipId);
            document.startFrame = model->getClipPosition(clipId);
            document.endFrame = document.startFrame + model->getClipPlaytime(clipId);
            document.metadata = QJsonObject{{QStringLiteral("clip_id"), clipId}, {QStringLiteral("track_id"), trackId},
                                            {QStringLiteral("bin_id"), model->getClipBinId(clipId)},
                                            {QStringLiteral("evidence_origin"), QStringLiteral("project_state")}};
            add(document);
        }
    }

    if (model->hasSubtitleModel()) {
        const std::shared_ptr<SubtitleModel> subtitles = model->getSubtitleModel();
        const double fps = pCore->getCurrentFps();
        if (subtitles && fps > 0.0) {
            for (int subtitleId : subtitles->getAllSubIds()) {
                VibeCutMediaDocument document;
                document.id = QStringLiteral("subtitle:%1").arg(subtitleId);
                document.kind = QStringLiteral("transcript");
                document.text = subtitles->getText(subtitleId);
                document.startFrame = subtitles->getSubtitlePosition(subtitleId).frames(fps);
                document.endFrame = subtitles->getSubtitleEnd(subtitleId);
                document.metadata = QJsonObject{{QStringLiteral("subtitle_id"), subtitleId},
                                                {QStringLiteral("layer"), subtitles->getLayerForId(subtitleId)},
                                                {QStringLiteral("evidence_origin"), QStringLiteral("subtitle_track")}};
                add(document);
            }
        }
    }

    QString evidenceError;
    const QJsonArray evidence = VibeCutMediaEvidence::loadCurrent(&evidenceError);
    if (!evidenceError.isEmpty()) {
        if (error) *error = QStringLiteral("Project/timeline index was rebuilt, but persistent media evidence was rejected: %1").arg(evidenceError);
        return false;
    }
    for (const QJsonValue &value : evidence) {
        VibeCutMediaEvidenceRecord record;
        QString recordError;
        if (!VibeCutMediaEvidenceRecord::fromJson(value.toObject(), record, &recordError)) {
            if (error) *error = QStringLiteral("Persistent media evidence failed validation during index rebuild: %1").arg(recordError);
            return false;
        }
        VibeCutMediaDocument document;
        document.id = QStringLiteral("evidence:%1").arg(record.id);
        document.kind = record.kind;
        document.text = record.text;
        document.startFrame = record.startFrame;
        document.endFrame = record.endFrame;
        document.metadata = record.metadata;
        document.metadata.insert(QStringLiteral("evidence_origin"), QStringLiteral("extractor"));
        document.metadata.insert(QStringLiteral("source_id"), record.sourceId);
        document.metadata.insert(QStringLiteral("source_fingerprint"), record.sourceFingerprint);
        document.metadata.insert(QStringLiteral("extractor_id"), record.extractorId);
        document.metadata.insert(QStringLiteral("extractor_version"), record.extractorVersion);
        document.metadata.insert(QStringLiteral("confidence"), record.confidence);
        document.metadata.insert(QStringLiteral("produced_utc"), record.producedUtc);
        add(document);
    }
    return true;
}
