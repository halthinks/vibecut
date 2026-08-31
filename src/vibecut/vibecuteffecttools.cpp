/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuteffecttools.h"

#include "core.h"
#include "effects/effectstack/model/effectitemmodel.hpp"
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

std::shared_ptr<EffectItemModel> effectAtRow(const std::shared_ptr<EffectStackModel> &stack, int row)
{
    if (!stack || row < 0 || row >= stack->rowCount()) return nullptr;
    return std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
}

QJsonObject inspectEffects(const QJsonObject &input)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    QJsonObject failure;
    const std::shared_ptr<EffectStackModel> stack = clipStack(clipId, failure);
    if (!stack) return failure;

    QJsonArray rows;
    for (int row = 0; row < stack->rowCount(); ++row) {
        const std::shared_ptr<EffectItemModel> effect = effectAtRow(stack, row);
        QDomDocument document;
        const QDomElement element = stack->rowToXml(row, document);
        if (!element.isNull()) document.appendChild(element);
        QJsonObject rowObject{{QStringLiteral("row"), row},
                              {QStringLiteral("xml"), document.toString(-1)},
                              {QStringLiteral("active"), row == stack->getActiveEffect()}};
        if (effect) {
            rowObject.insert(QStringLiteral("effect_id"), effect->getAssetId());
            rowObject.insert(QStringLiteral("enabled"), effect->isActive());
            rowObject.insert(QStringLiteral("parameters"), effect->toJson({}, true, false).object());
        }
        rows.append(rowObject);
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
    const int row = requestedRow >= 0 ? requestedRow : stack->effectRow(effectId);
    const std::shared_ptr<EffectItemModel> effect = effectAtRow(stack, row);
    if (!effect || effect->getAssetId() != effectId) {
        return err(QStringLiteral("Effect '%1'%2 was not found on clip %3. Call effects_inspect first.")
                       .arg(effectId, requestedRow >= 0 ? QStringLiteral(" at row %1").arg(requestedRow) : QString(), QString::number(clipId)));
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

QJsonObject setEffectParameter(const QJsonObject &input)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    const int row = input.value(QStringLiteral("row")).toInt(-1);
    const QString parameter = input.value(QStringLiteral("parameter")).toString().trimmed();
    const QString newValue = input.value(QStringLiteral("value")).toVariant().toString();
    if (row < 0) return err(QStringLiteral("row must be >= 0"));
    if (parameter.isEmpty()) return err(QStringLiteral("parameter must not be empty"));

    QJsonObject failure;
    const std::shared_ptr<EffectStackModel> stack = clipStack(clipId, failure);
    if (!stack) return failure;
    const std::shared_ptr<EffectItemModel> effect = effectAtRow(stack, row);
    if (!effect) return err(QStringLiteral("No effect exists at row %1 on clip %2.").arg(row).arg(clipId));
    const QModelIndex parameterIndex = effect->getParamIndexFromName(parameter);
    if (!parameterIndex.isValid()) {
        return err(QStringLiteral("Effect '%1' has no editable parameter named '%2'. Call effects_inspect first.")
                       .arg(effect->getAssetId(), parameter));
    }

    const QString oldValue = effect->getParam(parameter);
    if (oldValue == newValue) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId}, {QStringLiteral("row"), row},
                           {QStringLiteral("effect_id"), effect->getAssetId()}, {QStringLiteral("parameter"), parameter},
                           {QStringLiteral("old_value"), oldValue}, {QStringLiteral("value"), newValue},
                           {QStringLiteral("changed"), false}, {QStringLiteral("verified"), true}};
    }

    effect->setParameter(parameter, newValue);
    const QString liveValue = effect->getParam(parameter);
    if (liveValue != newValue) {
        effect->setParameter(parameter, oldValue);
        return err(QStringLiteral("Effect parameter update did not verify: requested '%1', live value '%2'.").arg(newValue, liveValue));
    }

    const std::shared_ptr<EffectItemModel> retained = effect;
    Fun undo = [retained, parameter, oldValue]() {
        retained->setParameter(parameter, oldValue);
        return retained->getParam(parameter) == oldValue;
    };
    Fun redo = [retained, parameter, newValue]() {
        retained->setParameter(parameter, newValue);
        return retained->getParam(parameter) == newValue;
    };
    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: set %1 parameter").arg(effect->getAssetId()));

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId}, {QStringLiteral("row"), row},
                       {QStringLiteral("effect_id"), effect->getAssetId()}, {QStringLiteral("parameter"), parameter},
                       {QStringLiteral("old_value"), oldValue}, {QStringLiteral("value"), newValue},
                       {QStringLiteral("changed"), true}, {QStringLiteral("verified"), true}};
}
}

bool registerVibeCutEffectTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject inspectInput{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), QJsonObject{{QStringLiteral("clip_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("clip_id")}},
                                   {QStringLiteral("additionalProperties"), false}};
    const QJsonObject inspectSchema{{QStringLiteral("name"), QStringLiteral("effects_inspect")},
                                    {QStringLiteral("description"), QStringLiteral("Inspect the complete Kdenlive effect stack for one timeline clip. Returns stack status, stable effect ids, rows, parameter JSON and Kdenlive XML. Read-only; use before changing/removing effects instead of guessing current state.")},
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
                                   {QStringLiteral("description"), QStringLiteral("Remove one known effect from a clip's Kdenlive effect stack by stable effect id and optional row. Undoable and live-stack-count verified. Call effects_inspect first when there may be duplicate effects.")},
                                   {QStringLiteral("input_schema"), removeInput}};
    VibeCutToolPolicy removePolicy;
    removePolicy.name = QStringLiteral("effect_remove");
    removePolicy.risk = VibeCutToolRisk::ReversibleEdit;
    removePolicy.reversible = true;
    removePolicy.mutatesProject = true;
    if (!surface.registerTool(removeSchema, removePolicy, removeEffect, error)) return false;

    const QJsonObject paramInput{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), QJsonObject{
                                     {QStringLiteral("clip_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                     {QStringLiteral("row"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                     {QStringLiteral("parameter"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                     {QStringLiteral("value"), QJsonObject{{QStringLiteral("description"), QStringLiteral("New parameter value; converted to the effect's string parameter representation.")}}}}},
                                 {QStringLiteral("required"), QJsonArray{QStringLiteral("clip_id"), QStringLiteral("row"), QStringLiteral("parameter"), QStringLiteral("value")}},
                                 {QStringLiteral("additionalProperties"), false}};
    const QJsonObject paramSchema{{QStringLiteral("name"), QStringLiteral("effect_parameter_set")},
                                  {QStringLiteral("description"), QStringLiteral("Set one existing effect parameter by inspected effect row and parameter name. Captures the prior value, verifies the live value, and creates a Kdenlive undo/redo command. Call effects_inspect first.")},
                                  {QStringLiteral("input_schema"), paramInput}};
    VibeCutToolPolicy paramPolicy;
    paramPolicy.name = QStringLiteral("effect_parameter_set");
    paramPolicy.risk = VibeCutToolRisk::ReversibleEdit;
    paramPolicy.reversible = true;
    paramPolicy.mutatesProject = true;
    return surface.registerTool(paramSchema, paramPolicy, setEffectParameter, error);
}
