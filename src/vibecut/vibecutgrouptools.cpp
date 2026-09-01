/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutgrouptools.h"

#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>
#include <QVector>
#include <unordered_map>
#include <unordered_set>

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

bool parseItems(const std::shared_ptr<TimelineItemModel> &model, const QJsonArray &values, std::unordered_set<int> &ids, QString &error)
{
    if (!model) {
        error = QStringLiteral("No timeline is open.");
        return false;
    }
    for (const QJsonValue &value : values) {
        const int id = value.toInt(-1);
        if (id < 0 || !model->isItem(id)) {
            error = QStringLiteral("Timeline item id %1 does not exist or is not a groupable clip/composition.").arg(id);
            return false;
        }
        ids.insert(id);
    }
    if (ids.size() < 2) {
        error = QStringLiteral("At least two distinct timeline item ids are required.");
        return false;
    }
    return true;
}

QJsonObject groupItems(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    std::unordered_set<int> ids;
    QString error;
    if (!parseItems(model, input.value(QStringLiteral("item_ids")).toArray(), ids, error)) return err(error);

    const int groupId = model->requestClipsGroup(ids, true, GroupType::Normal);
    if (groupId < 0 || !model->isGroup(groupId)) {
        return err(QStringLiteral("Kdenlive rejected grouping the requested items."));
    }
    for (int id : ids) {
        if (!model->isInGroup(id)) {
            return err(QStringLiteral("Grouping returned success but item %1 is not in a live group.").arg(id));
        }
    }
    QJsonArray grouped;
    for (int id : ids) grouped.append(id);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("group_id"), groupId},
                       {QStringLiteral("item_ids"), grouped}, {QStringLiteral("verified"), true}};
}

QJsonObject ungroupItems(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    std::unordered_set<int> ids;
    QString error;
    if (!parseItems(model, input.value(QStringLiteral("item_ids")).toArray(), ids, error)) return err(error);
    for (int id : ids) {
        if (!model->isInGroup(id)) {
            return err(QStringLiteral("Item %1 is not currently grouped. Call timeline inspection first.").arg(id));
        }
    }
    if (!model->requestClipsUngroup(ids, true)) {
        return err(QStringLiteral("Kdenlive rejected ungrouping the requested items."));
    }
    for (int id : ids) {
        if (model->isInGroup(id)) {
            return err(QStringLiteral("Ungroup returned success but item %1 is still grouped.").arg(id));
        }
    }
    QJsonArray ungrouped;
    for (int id : ids) ungrouped.append(id);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("item_ids"), ungrouped}, {QStringLiteral("verified"), true}};
}

QJsonObject moveGroup(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    const int groupId = input.value(QStringLiteral("group_id")).toInt(-1);
    const int anchorId = input.value(QStringLiteral("anchor_item_id")).toInt(-1);
    const int delta = input.value(QStringLiteral("delta_frames")).toInt(0);
    if (!model->isGroup(groupId)) return err(QStringLiteral("group_id %1 is not a live timeline group.").arg(groupId));
    if (!model->isItem(anchorId) || !model->isInGroup(anchorId)) return err(QStringLiteral("anchor_item_id must identify a grouped timeline clip/composition."));
    if (delta == 0) return err(QStringLiteral("delta_frames must be non-zero."));

    const std::unordered_set<int> leaves = model->getGroupElements(anchorId);
    if (leaves.size() < 2) return err(QStringLiteral("Anchor item does not resolve to a multi-item group."));
    std::unordered_map<int, int> oldPositions;
    QJsonArray preview;
    for (int id : leaves) {
        if (!model->isItem(id)) return err(QStringLiteral("Resolved group leaf %1 is not a movable timeline item.").arg(id));
        const int oldPos = model->getItemPosition(id);
        if (oldPos + delta < 0) return err(QStringLiteral("Group move would place item %1 before frame 0.").arg(id));
        oldPositions[id] = oldPos;
        preview.append(QJsonObject{{QStringLiteral("item_id"), id}, {QStringLiteral("old_position_frame"), oldPos},
                                   {QStringLiteral("position_frame"), oldPos + delta}, {QStringLiteral("track_id"), model->getItemTrackId(id)}});
    }
    if (input.value(QStringLiteral("dry_run")).toBool(false)) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("dry_run"), true}, {QStringLiteral("group_id"), groupId},
                           {QStringLiteral("anchor_item_id"), anchorId}, {QStringLiteral("delta_frames"), delta}, {QStringLiteral("items"), preview}};
    }

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    const QVector<int> allowedTracks;
    if (!model->requestGroupMove(anchorId, groupId, 0, delta, true, true, undo, redo, false, true, true, allowedTracks)) {
        undo();
        return err(QStringLiteral("Kdenlive rejected moving group %1 by %2 frames.").arg(groupId).arg(delta));
    }
    for (int id : leaves) {
        if (!model->isItem(id) || !model->isInGroup(id) || model->getItemPosition(id) != oldPositions[id] + delta) {
            undo();
            return err(QStringLiteral("Group move verification failed on item %1; operation rolled back.").arg(id));
        }
    }
    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: move timeline group"));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("group_id"), groupId}, {QStringLiteral("anchor_item_id"), anchorId},
                       {QStringLiteral("delta_frames"), delta}, {QStringLiteral("items"), preview}, {QStringLiteral("verified"), true}};
}

QJsonObject inputSchema()
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), QJsonObject{{QStringLiteral("item_ids"),
                           QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                       {QStringLiteral("minItems"), 2},
                                       {QStringLiteral("uniqueItems"), true},
                                       {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}}}}},
                       {QStringLiteral("required"), QJsonArray{QStringLiteral("item_ids")}},
                       {QStringLiteral("additionalProperties"), false}};
}

bool registerEditTool(VibeCutToolSurface &surface, const QString &name, const QString &description, const QJsonObject &schema,
                      const VibeCutToolSurface::Handler &handler, QString *error)
{
    VibeCutToolPolicy policy;
    policy.name = name;
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("description"), description},
                                            {QStringLiteral("input_schema"), schema}},
                                policy, handler, error);
}
} // namespace

bool registerVibeCutGroupTools(VibeCutToolSurface &surface, QString *error)
{
    if (!registerEditTool(surface, QStringLiteral("group_create"),
                          QStringLiteral("Group two or more existing timeline clips/compositions using Kdenlive's native undoable grouping operation and verify every item is grouped."),
                          inputSchema(), groupItems, error)) return false;
    if (!registerEditTool(surface, QStringLiteral("group_ungroup"),
                          QStringLiteral("Ungroup two or more existing grouped timeline clips/compositions using Kdenlive's native undoable ungroup operation and verify membership is removed."),
                          inputSchema(), ungroupItems, error)) return false;

    const QJsonObject moveSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), QJsonObject{
                                     {QStringLiteral("group_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                     {QStringLiteral("anchor_item_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                     {QStringLiteral("delta_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                     {QStringLiteral("dry_run"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}},
                                 {QStringLiteral("required"), QJsonArray{QStringLiteral("group_id"), QStringLiteral("anchor_item_id"), QStringLiteral("delta_frames")}},
                                 {QStringLiteral("additionalProperties"), false}};
    return registerEditTool(surface, QStringLiteral("group_move"),
                            QStringLiteral("Move an existing mixed timeline group horizontally by a relative frame delta using Kdenlive's native accumulated group-move operation. Preserves group/AV structure, supports dry-run, verifies every leaf moved by the same delta, and rolls back on mismatch."),
                            moveSchema, moveGroup, error);
}
