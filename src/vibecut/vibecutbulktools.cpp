/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutbulktools.h"

#include "core.h"
#include "mainwindow.h"
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

std::unordered_set<int> explicitOrSelected(const std::shared_ptr<TimelineItemModel> &model, const QJsonObject &input, QString &error)
{
    std::unordered_set<int> targets;
    if (input.contains(QStringLiteral("item_ids"))) {
        const QJsonArray ids = input.value(QStringLiteral("item_ids")).toArray();
        if (ids.isEmpty()) {
            error = QStringLiteral("item_ids must not be empty when provided.");
            return {};
        }
        for (const QJsonValue &value : ids) {
            const int id = value.toInt(-1);
            if (id < 0) {
                error = QStringLiteral("item_ids must contain non-negative timeline item ids.");
                return {};
            }
            if (!targets.insert(id).second) {
                error = QStringLiteral("item_ids contains duplicate id %1.").arg(id);
                return {};
            }
        }
    } else {
        targets = model->getCurrentSelection();
    }
    if (targets.empty()) error = QStringLiteral("No timeline items were supplied or selected.");
    return targets;
}

QJsonObject bulkDelete(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));

    QString targetError;
    std::unordered_set<int> targets = explicitOrSelected(model, input, targetError);
    if (!targetError.isEmpty()) return err(targetError);
    if (targets.size() > 500) return err(QStringLiteral("Bulk delete is limited to 500 items per governed operation."));

    std::vector<int> ordered(targets.begin(), targets.end());
    for (int id : ordered) {
        if (!model->isItem(id) && !model->isSubTitle(id)) {
            return err(QStringLiteral("Timeline item id %1 does not exist or is not deletable by this bulk operation.").arg(id));
        }
    }
    std::sort(ordered.begin(), ordered.end(), [model](int a, int b) {
        const int posA = model->getItemPosition(a);
        const int posB = model->getItemPosition(b);
        if (posA != posB) return posA > posB;
        return a > b;
    });

    QJsonArray preview;
    for (int id : ordered) {
        preview.append(QJsonObject{{QStringLiteral("item_id"), id},
                                   {QStringLiteral("position_frame"), model->getItemPosition(id)},
                                   {QStringLiteral("duration_frames"), model->getItemPlaytime(id)},
                                   {QStringLiteral("kind"), model->isClip(id) ? QStringLiteral("clip")
                                                                              : model->isComposition(id) ? QStringLiteral("composition")
                                                                                                         : QStringLiteral("subtitle")}});
    }
    if (input.value(QStringLiteral("dry_run")).toBool(false)) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("dry_run"), true},
                           {QStringLiteral("item_count"), static_cast<int>(ordered.size())},
                           {QStringLiteral("items"), preview}};
    }

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    for (int id : ordered) {
        if (!model->requestItemDeletion(id, undo, redo, false)) {
            undo();
            return err(QStringLiteral("Bulk delete failed on item %1; all earlier deletions were rolled back.").arg(id));
        }
    }

    for (int id : ordered) {
        if (model->isItem(id) || model->isSubTitle(id)) {
            undo();
            return err(QStringLiteral("Bulk delete verification failed because item %1 is still present; operation was rolled back.").arg(id));
        }
    }

    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: delete %1 timeline items").arg(ordered.size()));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("deleted_count"), static_cast<int>(ordered.size())},
                       {QStringLiteral("items"), preview}, {QStringLiteral("verified"), true}};
}

int steppedTrack(const std::shared_ptr<TimelineItemModel> &model, int startTrack, int steps)
{
    int track = startTrack;
    const int count = qAbs(steps);
    for (int i = 0; i < count; ++i) {
        const int next = steps > 0 ? model->getNextTrackId(track) : model->getPreviousTrackId(track);
        if (next == track) return -1;
        track = next;
    }
    return track;
}

QJsonObject bulkClipMove(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));

    QString targetError;
    std::unordered_set<int> targets = explicitOrSelected(model, input, targetError);
    if (!targetError.isEmpty()) return err(targetError);
    if (targets.size() > 200) return err(QStringLiteral("Bulk clip move is limited to 200 clips per governed operation."));

    const int deltaFrames = input.value(QStringLiteral("delta_frames")).toInt(0);
    const int trackSteps = input.value(QStringLiteral("track_steps")).toInt(0);
    if (deltaFrames == 0 && trackSteps == 0) return err(QStringLiteral("delta_frames and track_steps cannot both be zero."));

    struct MoveTarget {
        int id;
        int oldTrack;
        int oldPos;
        int newTrack;
        int newPos;
    };
    std::vector<MoveTarget> moves;
    moves.reserve(targets.size());

    for (int id : targets) {
        if (!model->isClip(id)) return err(QStringLiteral("bulk_clip_move accepts timeline clips only; item %1 is not a clip.").arg(id));
        if (model->isInGroup(id)) {
            return err(QStringLiteral("Clip %1 is grouped. Move the governed group instead of silently breaking group/AV structure.").arg(id));
        }
        const int oldTrack = model->getClipTrackId(id);
        const int oldPos = model->getClipPosition(id);
        const int newPos = oldPos + deltaFrames;
        if (newPos < 0) return err(QStringLiteral("Clip %1 would move before frame 0.").arg(id));
        const int newTrack = trackSteps == 0 ? oldTrack : steppedTrack(model, oldTrack, trackSteps);
        if (newTrack < 0 || !model->isTrack(newTrack)) {
            return err(QStringLiteral("Clip %1 cannot move %2 same-type track step(s) from track %3.").arg(id).arg(trackSteps).arg(oldTrack));
        }
        if (model->isAudioTrack(oldTrack) != model->isAudioTrack(newTrack)) {
            return err(QStringLiteral("Clip %1 target track type does not match its source track.").arg(id));
        }
        moves.push_back({id, oldTrack, oldPos, newTrack, newPos});
    }

    std::sort(moves.begin(), moves.end(), [deltaFrames](const MoveTarget &a, const MoveTarget &b) {
        if (a.oldPos == b.oldPos) return a.id < b.id;
        return deltaFrames >= 0 ? a.oldPos > b.oldPos : a.oldPos < b.oldPos;
    });

    QJsonArray preview;
    for (const MoveTarget &move : moves) {
        preview.append(QJsonObject{{QStringLiteral("clip_id"), move.id},
                                   {QStringLiteral("old_track_id"), move.oldTrack},
                                   {QStringLiteral("old_position_frame"), move.oldPos},
                                   {QStringLiteral("track_id"), move.newTrack},
                                   {QStringLiteral("position_frame"), move.newPos}});
    }
    if (input.value(QStringLiteral("dry_run")).toBool(false)) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("dry_run"), true},
                           {QStringLiteral("clip_count"), static_cast<int>(moves.size())}, {QStringLiteral("moves"), preview}};
    }

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    for (const MoveTarget &move : moves) {
        const TimelineModel::MoveResult result = model->requestClipMove(move.id, move.newTrack, move.newPos,
                                                                        false, true, true, true, undo, redo);
        if (result != TimelineModel::MoveSuccess) {
            undo();
            return err(QStringLiteral("Bulk clip move failed on clip %1; all earlier moves were rolled back.").arg(move.id));
        }
    }

    for (const MoveTarget &move : moves) {
        if (!model->isClip(move.id) || model->getClipTrackId(move.id) != move.newTrack || model->getClipPosition(move.id) != move.newPos) {
            undo();
            return err(QStringLiteral("Bulk clip move verification failed on clip %1; the operation was rolled back.").arg(move.id));
        }
    }

    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: move %1 clips").arg(moves.size()));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("moved_count"), static_cast<int>(moves.size())},
                       {QStringLiteral("moves"), preview}, {QStringLiteral("verified"), true}};
}

QJsonObject itemIdsSchema()
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                       {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                       {QStringLiteral("maxItems"), 500},
                       {QStringLiteral("description"), QStringLiteral("Explicit timeline item ids. If omitted, VibeCut uses the current timeline selection.")}};
}
} // namespace

bool registerVibeCutBulkTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject deleteInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{
                                      {QStringLiteral("item_ids"), itemIdsSchema()},
                                      {QStringLiteral("dry_run"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}},
                                  {QStringLiteral("additionalProperties"), false}};
    const QJsonObject deleteSchema{{QStringLiteral("name"), QStringLiteral("bulk_delete")},
                                   {QStringLiteral("description"), QStringLiteral("Delete an explicit set of clips/compositions/subtitles, or the current timeline selection, as one transactional Kdenlive undo command. Prevalidates every id, supports dry-run, rolls back on any failure, and verifies every target is gone before reporting success.")},
                                   {QStringLiteral("input_schema"), deleteInput}};
    VibeCutToolPolicy deletePolicy;
    deletePolicy.name = QStringLiteral("bulk_delete");
    deletePolicy.risk = VibeCutToolRisk::MajorEdit;
    deletePolicy.reversible = true;
    deletePolicy.mutatesProject = true;
    if (!surface.registerTool(deleteSchema, deletePolicy, bulkDelete, error)) return false;

    const QJsonObject moveInput{{QStringLiteral("type"), QStringLiteral("object")},
                                {QStringLiteral("properties"), QJsonObject{
                                    {QStringLiteral("item_ids"), itemIdsSchema()},
                                    {QStringLiteral("delta_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                    {QStringLiteral("track_steps"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), -32}, {QStringLiteral("maximum"), 32}}},
                                    {QStringLiteral("dry_run"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}},
                                {QStringLiteral("additionalProperties"), false}};
    const QJsonObject moveSchema{{QStringLiteral("name"), QStringLiteral("bulk_clip_move")},
                                 {QStringLiteral("description"), QStringLiteral("Move explicit or currently-selected ungrouped timeline clips by a relative frame delta and optional same-type track steps as one rollback-safe Kdenlive undo command. Preserves relative spacing, rejects grouped clips, supports dry-run, and verifies every final track/position.")},
                                 {QStringLiteral("input_schema"), moveInput}};
    VibeCutToolPolicy movePolicy;
    movePolicy.name = QStringLiteral("bulk_clip_move");
    movePolicy.risk = VibeCutToolRisk::ReversibleEdit;
    movePolicy.reversible = true;
    movePolicy.mutatesProject = true;
    return surface.registerTool(moveSchema, movePolicy, bulkClipMove, error);
}
