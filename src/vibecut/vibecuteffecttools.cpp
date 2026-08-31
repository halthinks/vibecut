/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuteffecttools.h"

#include "core.h"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <QDomDocument>
#include <QJsonArray>

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

std::shared_ptr<EffectStackModel> clipStack(int clipId, QJsonObject &failure)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) {
        failure = err(QStringLiteral("No timeline is open."));
        return nullptr;
    }
    if (!model->isClip(clipId)) {
        failure = err(QStringLiteral("Clip id %1 does not exist on the active timeline.").arg(clipId));
        return nullptr;
    }
    const std::shared_ptr<EffectStackModel> stack = model->getClipEffectStack(clipId);
    if (!stack) failure = err(QStringLiteral("Effect stack for clip %1 is unavailable.").arg(clipId));
    return stack;
}

QJsonObject inspectEffects(const QJsonObject &input)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    QJsonObject failure;
    const std::shared_ptr<EffectStackModel> stack = clipStack(clipId, failure);
    if (!stack) return failure;

    QJsonArray rows;
    for (int row = 0; row < stack->rowCount(); ++row) {
        QDomDocument document;
        const QDomElement element = stack->rowToXml(row, document);
        if (!element.isNull()) document.appendChild(element);
        rows.append(QJsonObject{{QStringLiteral("row"), row},
                                {QStringLiteral("xml"), document.toString(-1)},
                                {QStringLiteral("active"), row == stack->getActiveEffect()}});
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId},
                       {QStringLiteral("stack_enabled"), stack->isStackEnabled()},
                       {QStringLiteral("effect_count"), stack->rowCount()},
                       {QStringLiteral("effect_names"), stack->effectNames()},
                       {QStringLiteral("effects"), rows}};
}

QJsonObject removeEffect(const QJsonObject &input)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    const QString effectId = input.value(QStringLiteral("effect_id")).toString().trimmed();
    const int requestedRow = input.contains(QStringLiteral("row")) ? input.value(QStringLiteral("row")).toInt(-1) : -1;
    if (effectId.isEmpty()) return err(QStringLiteral("effect_id must not be empty"));

    QJsonObject failure;
    const std::shared_ptr<EffectStackModel> stack = clipStack(clipId, failure);
    if (!stack) return failure;
    int row = requestedRow >= 0 ? requestedRow : stack->effectRow(effectId);
    if (row < 0 || row >= stack->rowCount()) {
        return err(QStringLiteral("Effect '%1' was not found on clip %2. Call effects_inspect first.").arg(effectId).arg(clipId));
    }
    if (requestedRow >= 0 && stack->effectRow(effectId) != requestedRow && stack->effectRow(effectId) < 0) {
        return err(QStringLiteral("Requested effect id/row combination does not exist."));
    }

    const int before = stack->rowCount();
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QString effectName;
    stack->removeEffectWithUndo(effectId, effectName, row, undo, redo);
    if (stack->rowCount() != before - 1) {
        undo();
        return err(QStringLiteral("Effect removal did not produce the expected live stack change."));
    }
    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: remove effect %1").arg(effectName.isEmpty() ? effectId : effectName));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId},
                       {QStringLiteral("effect_id"), effectId}, {QStringLiteral("effect_name"), effectName},
                       {QStringLiteral("removed_row"), row}, {QStringLiteral("verified"), true}};
}
}

bool registerVibeCutEffectTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject inspectInput{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), QJsonObject{{QStringLiteral("clip_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("clip_id")}},
                                   {QStringLiteral("additionalProperties"), false}};
    const QJsonObject inspectSchema{{QStringLiteral("name"), QStringLiteral("effects_inspect")},
                                    {QStringLiteral("description"), QStringLiteral("Inspect the complete Kdenlive effect stack for one timeline clip. Returns stack status, effect names, rows and Kdenlive XML including effect parameters. Read-only; use before changing/removing effects instead of guessing current state.")},
                                    {QStringLiteral("input_schema"), inspectInput}};
    VibeCutToolPolicy inspectPolicy;
    inspectPolicy.name = QStringLiteral("effects_inspect");
    inspectPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(inspectSchema, inspectPolicy, inspectEffects, error)) return false;

    const QJsonObject removeInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{
                                      {QStringLiteral("clip_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                      {QStringLiteral("effect_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                      {QStringLiteral("row"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("clip_id"), QStringLiteral("effect_id")}},
                                  {QStringLiteral("additionalProperties"), false}};
    const QJsonObject removeSchema{{QStringLiteral("name"), QStringLiteral("effect_remove")},
                                   {QStringLiteral("description"), QStringLiteral("Remove one known effect from a clip's Kdenlive effect stack by effect id and optional row. Undoable and live-stack-count verified. Call effects_inspect first when there may be duplicate effects.")},
                                   {QStringLiteral("input_schema"), removeInput}};
    VibeCutToolPolicy removePolicy;
    removePolicy.name = QStringLiteral("effect_remove");
    removePolicy.risk = VibeCutToolRisk::ReversibleEdit;
    removePolicy.reversible = true;
    removePolicy.mutatesProject = true;
    return surface.registerTool(removeSchema, removePolicy, removeEffect, error);
}
