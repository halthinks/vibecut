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
    if (needle.isEmpty()) {
        return hits;
    }
    const QStringList tokens = needle.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const VibeCutMediaDocument &document : m_documents) {
        int score = 0;
        if (document.text.contains(needle, Qt::CaseInsensitive)) {
            score += 1000;
        }
        for (const QString &token : tokens) {
            if (token.size() > 1 && document.text.contains(token, Qt::CaseInsensitive)) {
                score += 25;
            }
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
    while (hits.size() > qBound(1, limit, 100)) {
        hits.removeLast();
    }
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
                                            {QStringLiteral("bin_id"), model->getClipBinId(clipId)}};
            add(document);
        }
    }

    if (model->hasSubtitleModel()) {
        const std::shared_ptr<SubtitleModel> subtitles = model->getSubtitleModel();
        if (subtitles) {
            for (int subtitleId : subtitles->getAllSubIds()) {
                VibeCutMediaDocument document;
                document.id = QStringLiteral("subtitle:%1").arg(subtitleId);
                document.kind = QStringLiteral("transcript");
                document.text = subtitles->getText(subtitleId);
                document.startFrame = model->getSubtitlePosition(subtitleId);
                document.endFrame = subtitles->getSubtitleEnd(subtitleId);
                document.metadata = QJsonObject{{QStringLiteral("subtitle_id"), subtitleId},
                                                {QStringLiteral("layer"), subtitles->getLayerForId(subtitleId)}};
                add(document);
            }
        }
    }
    return true;
}
