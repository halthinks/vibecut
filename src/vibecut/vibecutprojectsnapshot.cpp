/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecutprojectsnapshot.h"

#include "bin/model/subtitlemodel.hpp"
#include "core.h"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"

QJsonObject VibeCutProjectDiff::toJson() const
{
    return QJsonObject{{QStringLiteral("revision_delta"), revisionDelta},
                       {QStringLiteral("duration_frames_delta"), durationFramesDelta},
                       {QStringLiteral("clips_delta"), clipsDelta},
                       {QStringLiteral("tracks_delta"), tracksDelta},
                       {QStringLiteral("subtitles_delta"), subtitlesDelta},
                       {QStringLiteral("effects_delta"), effectsDelta}};
}

QString VibeCutProjectDiff::summary() const
{
    return QStringLiteral("Δ clips %1%2, subtitles %3%4, effects %5%6, duration %7%8 frames")
        .arg(clipsDelta >= 0 ? QStringLiteral("+") : QString()).arg(clipsDelta)
        .arg(subtitlesDelta >= 0 ? QStringLiteral("+") : QString()).arg(subtitlesDelta)
        .arg(effectsDelta >= 0 ? QStringLiteral("+") : QString()).arg(effectsDelta)
        .arg(durationFramesDelta >= 0 ? QStringLiteral("+") : QString()).arg(durationFramesDelta);
}

QJsonObject VibeCutProjectSnapshot::toJson() const
{
    return QJsonObject{{QStringLiteral("available"), available},
                       {QStringLiteral("revision"), static_cast<qint64>(revision)},
                       {QStringLiteral("duration_frames"), durationFrames},
                       {QStringLiteral("clips"), clips},
                       {QStringLiteral("tracks"), tracks},
                       {QStringLiteral("subtitles"), subtitles},
                       {QStringLiteral("effects"), effects}};
}

VibeCutProjectDiff VibeCutProjectSnapshot::diffTo(const VibeCutProjectSnapshot &after) const
{
    VibeCutProjectDiff diff;
    diff.revisionDelta = static_cast<qint64>(after.revision) - static_cast<qint64>(revision);
    diff.durationFramesDelta = after.durationFrames - durationFrames;
    diff.clipsDelta = after.clips - clips;
    diff.tracksDelta = after.tracks - tracks;
    diff.subtitlesDelta = after.subtitles - subtitles;
    diff.effectsDelta = after.effects - effects;
    return diff;
}

VibeCutProjectSnapshot VibeCutProjectSnapshot::capture(quint64 revisionToken)
{
    VibeCutProjectSnapshot snapshot;
    snapshot.revision = revisionToken;
    if (!pCore || !pCore->window()) {
        return snapshot;
    }
    TimelineWidget *timeline = pCore->window()->getCurrentTimeline();
    const std::shared_ptr<TimelineItemModel> model = timeline ? timeline->model() : nullptr;
    if (!model) {
        return snapshot;
    }

    snapshot.available = true;
    snapshot.durationFrames = model->duration();
    snapshot.tracks = model->getTracksCount();
    snapshot.clips = model->getClipsCount();
    if (model->hasSubtitleModel()) {
        const std::shared_ptr<SubtitleModel> subtitles = model->getSubtitleModel();
        snapshot.subtitles = subtitles ? subtitles->count() : 0;
    }
    for (int trackId : model->getAllTracksIds()) {
        for (int clipId : model->getItemsInRange(trackId, 0, -1, false)) {
            if (!model->isClip(clipId)) {
                continue;
            }
            const std::shared_ptr<EffectStackModel> stack = model->getClipEffectStack(clipId);
            if (stack) {
                snapshot.effects += stack->rowCount();
            }
        }
    }
    return snapshot;
}
