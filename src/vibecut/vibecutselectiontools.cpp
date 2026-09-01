/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutselectiontools.h"

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

QJsonArray selectedJson(const std::shared_ptr<TimelineItemModel> &model)
{
    QJsonArray selected;
    if (!model) return selected;
    for (int id : model->getCurrentSelection()) {
        QString kind = QStringLiteral("unknown");
        if (model->isClip(id)) kind = QStringLiteral("clip");
        else if (model->isComposition(id)) kind = QStringLiteral("composition");
        else if (model->isSubTitle(id)) kind = QStringLiteral("subtitle");
        selected.append(QJsonObject{{QStringLiteral("item_id"), id}, {QStringLiteral("kind"), kind},
                                    {QStringLiteral("position_frame"), model->getItemPosition(id)},
                                    {QStringLiteral("duration_frames"), model->getItemPlaytime(id)}});
    }
    return selected;
}

QJsonObject listSelection(const QJsonObject &)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("selection"), selectedJson(model)}};
}

QJsonObject setSelection(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    std::unordered_set<int> ids;
    for (const QJsonValue &value : input.value(QStringLiteral("item_ids")).toArray()) {
        const int id = value.toInt(-1);
        if (id < 0 || (!model->isItem(id) && !model->isSubTitle(id))) {
            return err(QStringLiteral("Timeline item id %1 does not exist.").arg(id));
        }
        ids.insert(id);
    }
    if (!model->requestSetSelection(ids)) return err(QStringLiteral("Kdenlive rejected the requested timeline selection."));
    const std::unordered_set<int> live = model->getCurrentSelection();
    if (live != ids) return err(QStringLiteral("Selection change returned success but live selection did not match exactly."));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("selection"), selectedJson(model)}, {QStringLiteral("verified"), true}};
}

QJsonObject clearSelection(const QJsonObject &)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    if (!model->requestClearSelection(false)) return err(QStringLiteral("Kdenlive rejected clearing the timeline selection."));
    if (!model->getCurrentSelection().empty()) return err(QStringLiteral("Selection clear returned success but live selection is not empty."));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("selection"), QJsonArray{}}, {QStringLiteral("verified"), true}};
}
}

bool registerVibeCutSelectionTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy readPolicy;
    readPolicy.name = QStringLiteral("selection_list");
    readPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), QStringLiteral("selection_list")},
                                          {QStringLiteral("description"), QStringLiteral("List the current Kdenlive timeline selection with stable item ids, types, positions and durations. Read-only project inspection.")},
                                          {QStringLiteral("input_schema"), noArgs}},
                              readPolicy, listSelection, error)) return false;

    const QJsonObject setInput{{QStringLiteral("type"), QStringLiteral("object")},
                               {QStringLiteral("properties"), QJsonObject{{QStringLiteral("item_ids"),
                                   QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("uniqueItems"), true},
                                               {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}}}}},
                               {QStringLiteral("required"), QJsonArray{QStringLiteral("item_ids")}},
                               {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy setPolicy;
    setPolicy.name = QStringLiteral("selection_set");
    setPolicy.risk = VibeCutToolRisk::ReadOnly; // ephemeral editor UI state; no durable project mutation
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), QStringLiteral("selection_set")},
                                          {QStringLiteral("description"), QStringLiteral("Set the current Kdenlive timeline selection to exact stable item ids. This changes only ephemeral editor selection state, not project content, and verifies the live selection.")},
                                          {QStringLiteral("input_schema"), setInput}},
                              setPolicy, setSelection, error)) return false;

    VibeCutToolPolicy clearPolicy;
    clearPolicy.name = QStringLiteral("selection_clear");
    clearPolicy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), QStringLiteral("selection_clear")},
                                            {QStringLiteral("description"), QStringLiteral("Clear Kdenlive's current timeline selection. Ephemeral editor state only; verifies the live selection is empty.")},
                                            {QStringLiteral("input_schema"), noArgs}},
                                clearPolicy, clearSelection, error);
}
