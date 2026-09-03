/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecutprojectsnapshot.h"

#include "assets/model/assetparametermodel.hpp"
#include "bin/model/subtitlemodel.hpp"
#include "core.h"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/model/trackmodel.hpp"
#include "timeline2/view/timelinewidget.h"

#include <QDomDocument>
#include <QJsonArray>

#include <algorithm>

namespace {
std::shared_ptr<TimelineItemModel> currentTimelineModel()
{
    if (!pCore || !pCore->window()) {
        return nullptr;
    }
    TimelineWidget *timeline = pCore->window()->getCurrentTimeline();
    return timeline ? timeline->model() : nullptr;
}

QJsonObject effectStackState(const std::shared_ptr<EffectStackModel> &stack)
{
    if (!stack) {
        return QJsonObject{{QStringLiteral("available"), false}};
    }

    QDomDocument document;
    const QDomElement effects = stack->toXml(document);
    if (!effects.isNull()) {
        document.appendChild(effects);
    }
    return QJsonObject{{QStringLiteral("available"), true},
                       {QStringLiteral("count"), stack->rowCount()},
                       {QStringLiteral("enabled"), stack->isStackEnabled()},
                       {QStringLiteral("names"), stack->effectNames()},
                       {QStringLiteral("xml"), document.toString(-1)}};
}

QJsonObject clipState(const std::shared_ptr<TimelineItemModel> &model, int clipId)
{
    const std::pair<int, int> inOut = model->getClipInOut(clipId);
    return QJsonObject{{QStringLiteral("kind"), QStringLiteral("clip")},
                       {QStringLiteral("item_id"), clipId},
                       {QStringLiteral("bin_id"), model->getClipBinId(clipId)},
                       {QStringLiteral("name"), model->getClipName(clipId)},
                       {QStringLiteral("position_frame"), model->getClipPosition(clipId)},
                       {QStringLiteral("duration_frames"), model->getItemPlaytime(clipId)},
                       {QStringLiteral("source_in_frame"), inOut.first},
                       {QStringLiteral("source_out_frame"), inOut.second},
                       {QStringLiteral("speed"), model->getClipSpeed(clipId)},
                       {QStringLiteral("effects"), effectStackState(model->getClipEffectStack(clipId))}};
}

QJsonObject compositionState(const std::shared_ptr<TimelineItemModel> &model, int compositionId)
{
    QJsonObject state{{QStringLiteral("kind"), QStringLiteral("composition")},
                      {QStringLiteral("item_id"), compositionId},
                      {QStringLiteral("position_frame"), model->getCompositionPosition(compositionId)},
                      {QStringLiteral("duration_frames"), model->getCompositionPlaytime(compositionId)}};
    const std::shared_ptr<AssetParameterModel> parameters = model->getCompositionParameterModel(compositionId);
    if (parameters) {
        state.insert(QStringLiteral("asset_id"), parameters->getAssetId());
        state.insert(QStringLiteral("active"), parameters->isActive());
        state.insert(QStringLiteral("parameters_json"), QString::fromUtf8(parameters->toJson().toJson(QJsonDocument::Compact)));
    }
    return state;
}

QJsonArray subtitleState(const std::shared_ptr<TimelineItemModel> &model)
{
    QJsonArray result;
    if (!model->hasSubtitleModel()) {
        return result;
    }
    const std::shared_ptr<SubtitleModel> subtitles = model->getSubtitleModel();
    if (!subtitles) {
        return result;
    }

    QVector<int> ids;
    const std::unordered_set<int> sourceIds = subtitles->getAllSubIds();
    ids.reserve(static_cast<int>(sourceIds.size()));
    for (int id : sourceIds) {
        ids.append(id);
    }
    std::sort(ids.begin(), ids.end(), [subtitles](int left, int right) {
        const int leftLayer = subtitles->getLayerForId(left);
        const int rightLayer = subtitles->getLayerForId(right);
        if (leftLayer != rightLayer) {
            return leftLayer < rightLayer;
        }
        const QPair<int, int> leftInOut = subtitles->getInOut(left);
        const QPair<int, int> rightInOut = subtitles->getInOut(right);
        if (leftInOut.first != rightInOut.first) {
            return leftInOut.first < rightInOut.first;
        }
        return left < right;
    });

    for (int id : ids) {
        const QPair<int, int> inOut = subtitles->getInOut(id);
        result.append(QJsonObject{{QStringLiteral("item_id"), id},
                                  {QStringLiteral("layer"), subtitles->getLayerForId(id)},
                                  {QStringLiteral("start_frame"), inOut.first},
                                  {QStringLiteral("end_frame"), inOut.second},
                                  {QStringLiteral("text"), subtitles->getText(id)},
                                  {QStringLiteral("style_name"), subtitles->getStyleName(id)},
                                  {QStringLiteral("name"), subtitles->getName(id)},
                                  {QStringLiteral("margin_left"), subtitles->getMarginL(id)},
                                  {QStringLiteral("margin_right"), subtitles->getMarginR(id)},
                                  {QStringLiteral("margin_vertical"), subtitles->getMarginV(id)},
                                  {QStringLiteral("effects"), subtitles->getEffects(id)},
                                  {QStringLiteral("is_dialogue"), subtitles->getIsDialogue(id)}});
    }
    return result;
}
} // namespace

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
    const std::shared_ptr<TimelineItemModel> model = currentTimelineModel();
    if (!model) {
        return snapshot;
    }

    snapshot.available = true;
    snapshot.durationFrames = model->duration();
    snapshot.tracks = model->getTracksCount();
    snapshot.clips = model->getClipsCount();
    if (model->hasSubtitleModel()) {
        const std::shared_ptr<SubtitleModel> subtitles = model->getSubtitleModel();
        snapshot.subtitles = subtitles ? static_cast<int>(subtitles->getAllSubIds().size()) : 0;
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

QJsonObject VibeCutProjectSnapshot::captureMutationStateV1()
{
    QJsonObject state{{QStringLiteral("schema"), QStringLiteral("vibecut_mutation_state_v1")},
                      {QStringLiteral("available"), false}};
    const std::shared_ptr<TimelineItemModel> model = currentTimelineModel();
    if (!model) {
        return state;
    }

    state.insert(QStringLiteral("available"), true);
    state.insert(QStringLiteral("duration_frames"), model->duration());
    state.insert(QStringLiteral("groups_data"), model->groupsData());
    state.insert(QStringLiteral("master_effects"), effectStackState(model->getMasterEffectStackModel()));

    QJsonArray tracks;
    for (int trackPosition = 0; trackPosition < model->getTracksCount(); ++trackPosition) {
        const int trackId = model->getTrackIndexFromPosition(trackPosition);
        if (!model->isTrack(trackId)) {
            continue;
        }
        const std::shared_ptr<TrackModel> track = model->getTrackById_const(trackId);
        if (!track) {
            continue;
        }

        QJsonObject trackState{{QStringLiteral("track_id"), trackId},
                               {QStringLiteral("position"), trackPosition},
                               {QStringLiteral("name"), model->getTrackFullName(trackId)},
                               {QStringLiteral("audio"), track->isAudioTrack()},
                               {QStringLiteral("locked"), track->isLocked()},
                               {QStringLiteral("timeline_active"), track->isTimelineActive()},
                               {QStringLiteral("hidden"), track->isHidden()},
                               {QStringLiteral("muted"), track->isMute()},
                               {QStringLiteral("mix_count"), track->mixCount()},
                               {QStringLiteral("effects"), effectStackState(model->getTrackEffectStackModel(trackId))}};

        QVector<int> itemIds;
        const std::unordered_set<int> sourceIds = model->getItemsInRange(trackId, 0, -1, true);
        itemIds.reserve(static_cast<int>(sourceIds.size()));
        for (int itemId : sourceIds) {
            if (model->isClip(itemId) || model->isComposition(itemId)) {
                itemIds.append(itemId);
            }
        }
        std::sort(itemIds.begin(), itemIds.end(), [model](int left, int right) {
            const int leftPosition = model->getItemPosition(left);
            const int rightPosition = model->getItemPosition(right);
            if (leftPosition != rightPosition) {
                return leftPosition < rightPosition;
            }
            const int leftKind = model->isClip(left) ? 0 : 1;
            const int rightKind = model->isClip(right) ? 0 : 1;
            if (leftKind != rightKind) {
                return leftKind < rightKind;
            }
            return left < right;
        });

        QJsonArray items;
        for (int itemId : itemIds) {
            items.append(model->isClip(itemId) ? clipState(model, itemId) : compositionState(model, itemId));
        }
        trackState.insert(QStringLiteral("items"), items);
        tracks.append(trackState);
    }
    state.insert(QStringLiteral("tracks"), tracks);

    if (model->hasSubtitleModel()) {
        const std::shared_ptr<SubtitleModel> subtitles = model->getSubtitleModel();
        if (subtitles) {
            state.insert(QStringLiteral("subtitle_locked"), subtitles->isLocked());
            state.insert(QStringLiteral("subtitle_disabled"), subtitles->isDisabled());
            state.insert(QStringLiteral("subtitles"), subtitleState(model));
        }
    } else {
        state.insert(QStringLiteral("subtitles"), QJsonArray());
    }
    return state;
}
