/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutbuseffecttools.h"

#include "core.h"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "effects/effectsrepository.hpp"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

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

std::shared_ptr<EffectStackModel> stackFor(const std::shared_ptr<TimelineItemModel> &model, const QJsonObject &input, QString &label, QJsonObject &failure)
{
    const QString scope = input.value(QStringLiteral("scope")).toString().trimmed().toLower();
    if (scope == QLatin1String("master")) {
        label = QStringLiteral("master");
        const std::shared_ptr<EffectStackModel> stack = model->getMasterEffectStackModel();
        if (!stack) failure = err(QStringLiteral("Timeline master effect stack is unavailable."));
        return stack;
    }
    if (scope != QLatin1String("track")) {
        failure = err(QStringLiteral("scope must be 'track' or 'master'."));
        return nullptr;
    }
    const int trackId = input.value(QStringLiteral("track_id")).toInt(-1);
    if (!model->isTrack(trackId)) {
        failure = err(QStringLiteral("track_id %1 does not identify an active timeline track.").arg(trackId));
        return nullptr;
    }
    label = QStringLiteral("track:%1").arg(trackId);
    const std::shared_ptr<EffectStackModel> stack = model->getTrackEffectStackModel(trackId);
    if (!stack) failure = err(QStringLiteral("Effect stack for track %1 is unavailable.").arg(trackId));
    return stack;
}

std::shared_ptr<EffectItemModel> effectAt(const std::shared_ptr<EffectStackModel> &stack, int row)
{
    if (!stack || row < 0 || row >= stack->rowCount()) return nullptr;
    return std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
}

QJsonObject inspect(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    QString label;
    QJsonObject failure;
    const std::shared_ptr<EffectStackModel> stack = stackFor(model, input, label, failure);
    if (!stack) return failure;

    QJsonArray effects;
    for (int row = 0; row < stack->rowCount(); ++row) {
        const auto effect = effectAt(stack, row);
        if (!effect) continue;
        effects.append(QJsonObject{{QStringLiteral("row"), row},
                                   {QStringLiteral("effect_id"), effect->getAssetId()},
                                   {QStringLiteral("enabled"), effect->isActive()},
                                   {QStringLiteral("built_in"), effect->isBuiltIn()},
                                   {QStringLiteral("parameters"), effect->toJson({}, true, false).object()}});
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("scope"), label},
                       {QStringLiteral("stack_enabled"), stack->isStackEnabled()},
                       {QStringLiteral("effect_count"), stack->rowCount()}, {QStringLiteral("effects"), effects}};
}

QJsonObject addEffect(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    QString label;
    QJsonObject failure;
    const std::shared_ptr<EffectStackModel> stack = stackFor(model, input, label, failure);
    if (!stack) return failure;

    const QString effectId = input.value(QStringLiteral("effect_id")).toString().trimmed();
    if (effectId.isEmpty() || !EffectsRepository::get()->exists(effectId)) return err(QStringLiteral("effect_id must identify an installed Kdenlive effect."));
    if (EffectsRepository::get()->isGroup(effectId)) return err(QStringLiteral("Use individual effects on track/master buses for now; effect-group verification is currently clip-scoped."));

    stringMap params;
    const QJsonObject parameters = input.value(QStringLiteral("parameters")).toObject();
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) params.insert(it.key(), it.value().toVariant().toString());

    const int before = stack->rowCount();
    if (!stack->appendEffect(effectId, true, params)) return err(QStringLiteral("Kdenlive rejected effect '%1' on %2.").arg(effectId, label));
    if (stack->rowCount() != before + 1) return err(QStringLiteral("Effect add did not produce exactly one new live row on %1.").arg(label));
    const auto added = effectAt(stack, stack->rowCount() - 1);
    if (!added || added->getAssetId() != effectId) return err(QStringLiteral("Added effect row could not be verified as '%1'.").arg(effectId));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("scope"), label}, {QStringLiteral("effect_id"), effectId},
                       {QStringLiteral("row"), stack->rowCount() - 1}, {QStringLiteral("verified"), true}};
}

QJsonObject removeEffect(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    QString label;
    QJsonObject failure;
    const std::shared_ptr<EffectStackModel> stack = stackFor(model, input, label, failure);
    if (!stack) return failure;

    const int row = input.value(QStringLiteral("row")).toInt(-1);
    const auto effect = effectAt(stack, row);
    if (!effect) return err(QStringLiteral("No effect exists at row %1 on %2.").arg(row).arg(label));
    if (effect->isBuiltIn()) return err(QStringLiteral("Built-in effects are not removable through this tool."));
    const QString effectId = effect->getAssetId();
    const int before = stack->rowCount();
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QString effectName;
    stack->removeEffectWithUndo(effect, effectName, undo, redo);
    if (stack->rowCount() != before - 1) {
        undo();
        return err(QStringLiteral("Bus effect removal failed live verification."));
    }
    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: remove %1 effect").arg(label));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("scope"), label}, {QStringLiteral("effect_id"), effectId},
                       {QStringLiteral("removed_row"), row}, {QStringLiteral("verified"), true}};
}

QJsonObject setParameter(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    QString label;
    QJsonObject failure;
    const std::shared_ptr<EffectStackModel> stack = stackFor(model, input, label, failure);
    if (!stack) return failure;

    const int row = input.value(QStringLiteral("row")).toInt(-1);
    const QString parameter = input.value(QStringLiteral("parameter")).toString().trimmed();
    const QString value = input.value(QStringLiteral("value")).toVariant().toString();
    const auto effect = effectAt(stack, row);
    if (!effect) return err(QStringLiteral("No effect exists at row %1 on %2.").arg(row).arg(label));
    if (parameter.isEmpty() || !effect->getParamIndexFromName(parameter).isValid()) return err(QStringLiteral("Effect '%1' has no editable parameter '%2'.").arg(effect->getAssetId(), parameter));

    const QString oldValue = effect->getParam(parameter);
    if (oldValue == value) return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("scope"), label}, {QStringLiteral("changed"), false}, {QStringLiteral("verified"), true}};
    effect->setParameter(parameter, value);
    if (effect->getParam(parameter) != value) {
        effect->setParameter(parameter, oldValue);
        return err(QStringLiteral("Bus effect parameter update did not verify."));
    }
    const std::shared_ptr<EffectItemModel> retained = effect;
    Fun undo = [retained, parameter, oldValue]() { retained->setParameter(parameter, oldValue); return retained->getParam(parameter) == oldValue; };
    Fun redo = [retained, parameter, value]() { retained->setParameter(parameter, value); return retained->getParam(parameter) == value; };
    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: set %1 effect parameter").arg(label));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("scope"), label}, {QStringLiteral("row"), row},
                       {QStringLiteral("effect_id"), effect->getAssetId()}, {QStringLiteral("parameter"), parameter},
                       {QStringLiteral("old_value"), oldValue}, {QStringLiteral("value"), value}, {QStringLiteral("verified"), true}};
}

QJsonObject baseInput(const QJsonObject &extra)
{
    QJsonObject properties{{QStringLiteral("scope"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                 {QStringLiteral("enum"), QJsonArray{QStringLiteral("track"), QStringLiteral("master")}}}},
                           {QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}};
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) properties.insert(it.key(), it.value());
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), QJsonArray{QStringLiteral("scope")}}, {QStringLiteral("additionalProperties"), false}};
}

bool registerEdit(VibeCutToolSurface &surface, const QString &name, const QString &description, const QJsonObject &input,
                  const VibeCutToolSurface::Handler &handler, QString *error)
{
    VibeCutToolPolicy policy;
    policy.name = name;
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("description"), description}, {QStringLiteral("input_schema"), input}}, policy, handler, error);
}
} // namespace

bool registerVibeCutBusEffectTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject inspectSchema{{QStringLiteral("name"), QStringLiteral("bus_effects_inspect")},
                                    {QStringLiteral("description"), QStringLiteral("Inspect the native Kdenlive effect stack on a stable timeline track or the timeline master bus, including rows, ids, built-in state and parameter JSON. Read-only.")},
                                    {QStringLiteral("input_schema"), baseInput({})}};
    VibeCutToolPolicy inspectPolicy;
    inspectPolicy.name = QStringLiteral("bus_effects_inspect");
    inspectPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(inspectSchema, inspectPolicy, inspect, error)) return false;

    const QJsonObject addInput = baseInput(QJsonObject{{QStringLiteral("effect_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                                        {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}});
    if (!registerEdit(surface, QStringLiteral("bus_effect_add"), QStringLiteral("Add one installed individual Kdenlive effect to a track or master effect stack and verify the exact new row/id."), addInput, addEffect, error)) return false;

    const QJsonObject removeInput = baseInput(QJsonObject{{QStringLiteral("row"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}}});
    if (!registerEdit(surface, QStringLiteral("bus_effect_remove"), QStringLiteral("Remove one non-built-in effect row from a track or master bus using Kdenlive's undoable EffectStackModel path and verify the live stack size."), removeInput, removeEffect, error)) return false;

    const QJsonObject parameterInput = baseInput(QJsonObject{{QStringLiteral("row"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                                              {QStringLiteral("parameter"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                                              {QStringLiteral("value"), QJsonObject{}}});
    return registerEdit(surface, QStringLiteral("bus_effect_parameter_set"), QStringLiteral("Set one verified parameter on an existing track/master effect row with explicit undo/redo."), parameterInput, setParameter, error);
}
