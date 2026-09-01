/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuteffecttools.h"

#include "core.h"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "effects/effectsrepository.hpp"
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

QJsonObject listAvailableEffects(const QJsonObject &input)
{
    const QString query = input.value(QStringLiteral("query")).toString().trimmed();
    const bool audioOnly = input.value(QStringLiteral("audio_only")).toBool(false);
    const bool videoOnly = input.value(QStringLiteral("video_only")).toBool(false);
    QJsonArray effects;
    for (const QPair<QString, QString> &entry : EffectsRepository::get()->getNames()) {
        const QString id = entry.first;
        const QString name = entry.second;
        const bool isAudio = EffectsRepository::get()->isAudioEffect(id);
        if (audioOnly && !isAudio) continue;
        if (videoOnly && isAudio) continue;
        if (!query.isEmpty() && !id.contains(query, Qt::CaseInsensitive) && !name.contains(query, Qt::CaseInsensitive) &&
            !EffectsRepository::get()->getDescription(id).contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        effects.append(QJsonObject{{QStringLiteral("id"), id},
                                   {QStringLiteral("name"), name},
                                   {QStringLiteral("description"), EffectsRepository::get()->getDescription(id)},
                                   {QStringLiteral("audio"), isAudio},
                                   {QStringLiteral("group"), EffectsRepository::get()->isGroup(id)},
                                   {QStringLiteral("unique"), EffectsRepository::get()->isUnique(id)},
                                   {QStringLiteral("included"), EffectsRepository::get()->isIncludedInList(id)}});
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("effects"), effects}};
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

QJsonObject addEffect(const QJsonObject &input)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    const QString effectId = input.value(QStringLiteral("effect_id")).toString().trimmed();
    if (effectId.isEmpty()) return err(QStringLiteral("effect_id must not be empty"));
    if (!EffectsRepository::get()->exists(effectId)) {
        return err(QStringLiteral("Unknown installed Kdenlive effect '%1'. Call effects_available first.").arg(effectId));
    }
    if (EffectsRepository::get()->isGroup(effectId)) {
        return err(QStringLiteral("Effect groups are not yet accepted by effect_add because group expansion needs explicit per-child verification. Choose an individual installed effect id from effects_available."));
    }

    QJsonObject failure;
    const std::shared_ptr<EffectStackModel> stack = clipStack(clipId, failure);
    if (!stack) return failure;

    stringMap params;
    const QJsonObject parameters = input.value(QStringLiteral("parameters")).toObject();
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        params.insert(it.key(), it.value().toVariant().toString());
    }

    const int before = stack->rowCount();
    if (!stack->appendEffect(effectId, true, params)) {
        return err(QStringLiteral("Kdenlive rejected effect '%1' on clip %2. The effect may not match the clip's audio/video state or may violate an effect-specific constraint.")
                       .arg(effectId).arg(clipId));
    }
    const int after = stack->rowCount();
    if (after != before + 1) {
        return err(QStringLiteral("Effect add returned success but stack size changed from %1 to %2 instead of exactly one row.").arg(before).arg(after));
    }
    const std::shared_ptr<EffectItemModel> added = effectAtRow(stack, after - 1);
    if (!added || added->getAssetId() != effectId) {
        return err(QStringLiteral("Effect add returned success but the new live stack row could not be verified as '%1'.").arg(effectId));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId},
                       {QStringLiteral("effect_id"), effectId}, {QStringLiteral("effect_name"), EffectsRepository::get()->getName(effectId)},
                       {QStringLiteral("row"), after - 1}, {QStringLiteral("parameters"), added->toJson({}, true, false).object()},
                       {QStringLiteral("verified"), true}};
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
    const QJsonObject availableInput{{QStringLiteral("type"), QStringLiteral("object")},
                                     {QStringLiteral("properties"), QJsonObject{
                                         {QStringLiteral("query"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                         {QStringLiteral("audio_only"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
                                         {QStringLiteral("video_only"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}},
                                     {QStringLiteral("additionalProperties"), false}};
    const QJsonObject availableSchema{{QStringLiteral("name"), QStringLiteral("effects_available")},
                                      {QStringLiteral("description"), QStringLiteral("List/search the actual effects installed in this Kdenlive runtime with stable ids, names, descriptions and audio/group/unique metadata. Read-only; use before effect_add instead of inventing ids.")},
                                      {QStringLiteral("input_schema"), availableInput}};
    VibeCutToolPolicy availablePolicy;
    availablePolicy.name = QStringLiteral("effects_available");
    availablePolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(availableSchema, availablePolicy, listAvailableEffects, error)) return false;

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

    const QJsonObject addInput{{QStringLiteral("type"), QStringLiteral("object")},
                               {QStringLiteral("properties"), QJsonObject{
                                   {QStringLiteral("clip_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                   {QStringLiteral("effect_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                   {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                              {QStringLiteral("description"), QStringLiteral("Optional initial parameter name/value map. Values are converted to Kdenlive's string parameter representation.")}}}}},
                               {QStringLiteral("required"), QJsonArray{QStringLiteral("clip_id"), QStringLiteral("effect_id")}},
                               {QStringLiteral("additionalProperties"), false}};
    const QJsonObject addSchema{{QStringLiteral("name"), QStringLiteral("effect_add")},
                                {QStringLiteral("description"), QStringLiteral("Add one individual installed Kdenlive effect to a timeline clip using the native EffectStackModel. Kdenlive validates audio/video compatibility and effect-specific constraints; the new live row/id is verified. Call effects_available first. Effect groups are intentionally not accepted yet.")},
                                {QStringLiteral("input_schema"), addInput}};
    VibeCutToolPolicy addPolicy;
    addPolicy.name = QStringLiteral("effect_add");
    addPolicy.risk = VibeCutToolRisk::ReversibleEdit;
    addPolicy.reversible = true;
    addPolicy.mutatesProject = true;
    if (!surface.registerTool(addSchema, addPolicy, addEffect, error)) return false;

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
