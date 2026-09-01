/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutbulkcopytools.h"

#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelinefunctions.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

std::shared_ptr<TimelineItemModel> currentModel()
{
    if (!pCore || !pCore->window()) return nullptr;
    TimelineWidget *timeline = pCore->window()->getCurrentTimeline();
    return timeline ? timeline->model() : nullptr;
}

QJsonObject copyTo(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));

    std::unordered_set<int> targets;
    if (input.contains(QStringLiteral("clip_ids"))) {
        const QJsonArray values = input.value(QStringLiteral("clip_ids")).toArray();
        if (values.isEmpty()) return err(QStringLiteral("clip_ids must not be empty when supplied."));
        for (const QJsonValue &value : values) {
            const int id = value.toInt(-1);
            if (id < 0 || !model->isClip(id)) return err(QStringLiteral("clip_ids contains invalid/non-clip id %1.").arg(id));
            if (!targets.insert(id).second) return err(QStringLiteral("clip_ids contains duplicate id %1.").arg(id));
        }
    } else {
        for (int id : model->getCurrentSelection()) {
            if (!model->isClip(id)) return err(QStringLiteral("Current selection contains non-clip item %1; bulk_clip_copy_to copies clips only.").arg(id));
            targets.insert(id);
        }
    }
    if (targets.empty()) return err(QStringLiteral("No clips were supplied or selected."));
    if (targets.size() > 100) return err(QStringLiteral("Bulk clip copy is limited to 100 clips per operation."));

    int sourceTrack = -1;
    int sourceMinPos = -1;
    std::vector<int> ordered(targets.begin(), targets.end());
    for (int id : ordered) {
        if (model->isInGroup(id)) return err(QStringLiteral("Clip %1 is grouped; copy the governed group through a group-aware operation instead.").arg(id));
        const int track = model->getClipTrackId(id);
        if (sourceTrack < 0) sourceTrack = track;
        if (track != sourceTrack) return err(QStringLiteral("bulk_clip_copy_to currently requires all source clips on one track."));
        const int pos = model->getClipPosition(id);
        if (sourceMinPos < 0 || pos < sourceMinPos) sourceMinPos = pos;
    }
    std::sort(ordered.begin(), ordered.end(), [model](int a, int b) { return model->getClipPosition(a) < model->getClipPosition(b); });

    const int targetTrack = input.value(QStringLiteral("track_id")).toInt(-1);
    const int targetPos = input.value(QStringLiteral("position_frame")).toInt(-1);
    if (!model->isTrack(targetTrack)) return err(QStringLiteral("track_id must identify an existing timeline track."));
    if (targetPos < 0) return err(QStringLiteral("position_frame must be >= 0."));
    if (model->isAudioTrack(sourceTrack) != model->isAudioTrack(targetTrack)) {
        return err(QStringLiteral("Destination track type must match the source track type."));
    }

    QJsonArray preview;
    for (int id : ordered) {
        preview.append(QJsonObject{{QStringLiteral("clip_id"), id},
                                   {QStringLiteral("source_position_frame"), model->getClipPosition(id)},
                                   {QStringLiteral("expected_position_frame"), targetPos + (model->getClipPosition(id) - sourceMinPos)}});
    }
    if (input.value(QStringLiteral("dry_run")).toBool(false)) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("dry_run"), true},
                           {QStringLiteral("clip_count"), static_cast<int>(ordered.size())},
                           {QStringLiteral("track_id"), targetTrack}, {QStringLiteral("position_frame"), targetPos},
                           {QStringLiteral("clips"), preview}};
    }

    const QString payload = TimelineFunctions::copyClips(model, targets);
    if (payload.isEmpty()) return err(QStringLiteral("Kdenlive could not serialize the requested clips for copy."));

    const int beforeCount = model->getClipsCount();
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (!TimelineFunctions::pasteClips(model, payload, targetTrack, targetPos, undo, redo, 0, -1, true)) {
        undo();
        return err(QStringLiteral("Kdenlive rejected the transactional clip paste."));
    }

    const int afterCount = model->getClipsCount();
    const std::unordered_set<int> pasted = model->getCurrentSelection();
    if (afterCount - beforeCount != static_cast<int>(ordered.size()) || pasted.size() != ordered.size()) {
        undo();
        return err(QStringLiteral("Bulk copy verification failed: pasted clip count/selection did not match the requested set; operation rolled back."));
    }

    int observedMinPos = -1;
    QJsonArray pastedItems;
    for (int id : pasted) {
        if (!model->isClip(id) || model->getClipTrackId(id) != targetTrack) {
            undo();
            return err(QStringLiteral("Bulk copy verification failed: a pasted item is not a clip on the requested destination track."));
        }
        const int pos = model->getClipPosition(id);
        if (observedMinPos < 0 || pos < observedMinPos) observedMinPos = pos;
        pastedItems.append(QJsonObject{{QStringLiteral("clip_id"), id}, {QStringLiteral("position_frame"), pos}});
    }
    if (observedMinPos != targetPos) {
        undo();
        return err(QStringLiteral("Bulk copy verification failed: pasted selection did not start at the requested frame; operation rolled back."));
    }

    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: copy %1 clips").arg(ordered.size()));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("copied_count"), static_cast<int>(ordered.size())},
                       {QStringLiteral("track_id"), targetTrack}, {QStringLiteral("position_frame"), targetPos},
                       {QStringLiteral("pasted_clips"), pastedItems}, {QStringLiteral("verified"), true}};
}
} // namespace

bool registerVibeCutBulkCopyTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject schemaInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{
                                      {QStringLiteral("clip_ids"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                               {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                                                               {QStringLiteral("maxItems"), 100}}},
                                      {QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                      {QStringLiteral("position_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                      {QStringLiteral("dry_run"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("track_id"), QStringLiteral("position_frame")}},
                                  {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("bulk_clip_copy_to")},
                             {QStringLiteral("description"), QStringLiteral("Copy explicit or currently-selected ungrouped clips from one source track to an exact destination track/frame using Kdenlive's native clip serialization/paste with one undo command. Supports dry-run and verifies the newly pasted selection/count/anchor position.")},
                             {QStringLiteral("input_schema"), schemaInput}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("bulk_clip_copy_to");
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    return surface.registerTool(schema, policy, copyTo, error);
}
