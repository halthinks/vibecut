/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuttransitionparamtools.h"

#include "assets/model/assetparametermodel.hpp"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

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

std::shared_ptr<AssetParameterModel> parameterModel(int compositionId, QJsonObject &failure)
{
    const std::shared_ptr<TimelineItemModel> timeline = currentModel();
    if (!timeline) {
        failure = err(QStringLiteral("No timeline is open."));
        return nullptr;
    }
    if (!timeline->isComposition(compositionId)) {
        failure = err(QStringLiteral("Composition id %1 does not exist on the active timeline.").arg(compositionId));
        return nullptr;
    }
    const std::shared_ptr<AssetParameterModel> model = timeline->getCompositionParameterModel(compositionId);
    if (!model) failure = err(QStringLiteral("Composition %1 has no available parameter model.").arg(compositionId));
    return model;
}

QJsonObject inspect(const QJsonObject &input)
{
    const int compositionId = input.value(QStringLiteral("composition_id")).toInt(-1);
    QJsonObject failure;
    const std::shared_ptr<AssetParameterModel> model = parameterModel(compositionId, failure);
    if (!model) return failure;
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("composition_id"), compositionId},
                       {QStringLiteral("asset_id"), model->getAssetId()},
                       {QStringLiteral("parameters"), model->toJson({}, true, false).object()}};
}

QJsonObject setParameter(const QJsonObject &input)
{
    const int compositionId = input.value(QStringLiteral("composition_id")).toInt(-1);
    const QString parameter = input.value(QStringLiteral("parameter")).toString().trimmed();
    const QString newValue = input.value(QStringLiteral("value")).toVariant().toString();
    if (parameter.isEmpty()) return err(QStringLiteral("parameter must not be empty"));

    QJsonObject failure;
    const std::shared_ptr<AssetParameterModel> model = parameterModel(compositionId, failure);
    if (!model) return failure;
    const QModelIndex index = model->getParamIndexFromName(parameter);
    if (!index.isValid()) {
        return err(QStringLiteral("Composition '%1' has no editable parameter named '%2'. Call transition_parameters_inspect first.")
                       .arg(model->getAssetId(), parameter));
    }

    const QString oldValue = model->getParam(parameter);
    if (oldValue == newValue) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("composition_id"), compositionId},
                           {QStringLiteral("asset_id"), model->getAssetId()}, {QStringLiteral("parameter"), parameter},
                           {QStringLiteral("value"), newValue}, {QStringLiteral("changed"), false}, {QStringLiteral("verified"), true}};
    }

    model->setParameter(parameter, newValue);
    if (model->getParam(parameter) != newValue) {
        model->setParameter(parameter, oldValue);
        return err(QStringLiteral("Transition parameter update did not verify on the live parameter model."));
    }

    const std::shared_ptr<AssetParameterModel> retained = model;
    Fun undo = [retained, parameter, oldValue]() {
        retained->setParameter(parameter, oldValue);
        return retained->getParam(parameter) == oldValue;
    };
    Fun redo = [retained, parameter, newValue]() {
        retained->setParameter(parameter, newValue);
        return retained->getParam(parameter) == newValue;
    };
    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: set transition parameter %1").arg(parameter));

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("composition_id"), compositionId},
                       {QStringLiteral("asset_id"), model->getAssetId()}, {QStringLiteral("parameter"), parameter},
                       {QStringLiteral("old_value"), oldValue}, {QStringLiteral("value"), newValue},
                       {QStringLiteral("changed"), true}, {QStringLiteral("verified"), true}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required}, {QStringLiteral("additionalProperties"), false}};
}
} // namespace

bool registerVibeCutTransitionParameterTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject inspectInput = objectSchema(
        QJsonObject{{QStringLiteral("composition_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}},
        QJsonArray{QStringLiteral("composition_id")});
    const QJsonObject inspectSchema{{QStringLiteral("name"), QStringLiteral("transition_parameters_inspect")},
                                    {QStringLiteral("description"), QStringLiteral("Inspect the native AssetParameterModel for one timeline composition/transition, returning its installed asset id and editable parameter JSON. Read-only." )},
                                    {QStringLiteral("input_schema"), inspectInput}};
    VibeCutToolPolicy inspectPolicy;
    inspectPolicy.name = QStringLiteral("transition_parameters_inspect");
    inspectPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(inspectSchema, inspectPolicy, inspect, error)) return false;

    const QJsonObject setInput = objectSchema(
        QJsonObject{{QStringLiteral("composition_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                    {QStringLiteral("parameter"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                    {QStringLiteral("value"), QJsonObject{}}},
        QJsonArray{QStringLiteral("composition_id"), QStringLiteral("parameter"), QStringLiteral("value")});
    const QJsonObject setSchema{{QStringLiteral("name"), QStringLiteral("transition_parameter_set")},
                                {QStringLiteral("description"), QStringLiteral("Set one named native Kdenlive transition/composition parameter, verify the live value, and register undo/redo. Call transition_parameters_inspect first instead of guessing parameter names." )},
                                {QStringLiteral("input_schema"), setInput}};
    VibeCutToolPolicy setPolicy;
    setPolicy.name = QStringLiteral("transition_parameter_set");
    setPolicy.risk = VibeCutToolRisk::ReversibleEdit;
    setPolicy.reversible = true;
    setPolicy.mutatesProject = true;
    return surface.registerTool(setSchema, setPolicy, setParameter, error);
}
