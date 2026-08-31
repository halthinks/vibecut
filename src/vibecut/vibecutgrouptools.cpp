/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutgrouptools.h"

#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>
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

bool registerEditTool(VibeCutToolSurface &surface, const QString &name, const QString &description,
                      const VibeCutToolSurface::Handler &handler, QString *error)
{
    VibeCutToolPolicy policy;
    policy.name = name;
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("description"), description},
                                            {QStringLiteral("input_schema"), inputSchema()}},
                                policy, handler, error);
}
} // namespace

bool registerVibeCutGroupTools(VibeCutToolSurface &surface, QString *error)
{
    if (!registerEditTool(surface, QStringLiteral("group_create"),
                          QStringLiteral("Group two or more existing timeline clips/compositions using Kdenlive's native undoable grouping operation and verify every item is grouped."),
                          groupItems, error)) return false;
    return registerEditTool(surface, QStringLiteral("group_ungroup"),
                            QStringLiteral("Ungroup two or more existing grouped timeline clips/compositions using Kdenlive's native undoable ungroup operation and verify membership is removed."),
                            ungroupItems, error);
}
