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

QJsonObject bulkDelete(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));

    std::unordered_set<int> targets;
    if (input.contains(QStringLiteral("item_ids"))) {
        const QJsonArray ids = input.value(QStringLiteral("item_ids")).toArray();
        if (ids.isEmpty()) return err(QStringLiteral("item_ids must not be empty when provided."));
        for (const QJsonValue &value : ids) {
            const int id = value.toInt(-1);
            if (id < 0) return err(QStringLiteral("item_ids must contain non-negative timeline item ids."));
            if (!targets.insert(id).second) return err(QStringLiteral("item_ids contains duplicate id %1.").arg(id));
        }
    } else {
        targets = model->getCurrentSelection();
    }
    if (targets.empty()) return err(QStringLiteral("No timeline items were supplied or selected."));
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
} // namespace

bool registerVibeCutBulkTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("item_ids"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                         {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                                                         {QStringLiteral("maxItems"), 500},
                                                                         {QStringLiteral("description"), QStringLiteral("Explicit timeline item ids. If omitted, VibeCut uses the current timeline selection.")}}},
                                {QStringLiteral("dry_run"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}},
                            {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("bulk_delete")},
                             {QStringLiteral("description"), QStringLiteral("Delete an explicit set of clips/compositions/subtitles, or the current timeline selection, as one transactional Kdenlive undo command. Prevalidates every id, supports dry-run, rolls back on any failure, and verifies every target is gone before reporting success.")},
                             {QStringLiteral("input_schema"), input}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("bulk_delete");
    policy.risk = VibeCutToolRisk::MajorEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    return surface.registerTool(schema, policy, bulkDelete, error);
}
