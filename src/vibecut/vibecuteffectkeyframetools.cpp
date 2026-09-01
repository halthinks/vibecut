/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuteffectkeyframetools.h"

#include "assets/keyframes/model/keyframemodellist.hpp"
#include "core.h"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
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

struct Target
{
    std::shared_ptr<EffectStackModel> stack;
    std::shared_ptr<EffectItemModel> effect;
    std::shared_ptr<KeyframeModelList> keyframes;
};

bool targetFor(int clipId, int row, Target &target, QJsonObject &failure)
{
    if (!pCore || !pCore->window()) {
        failure = err(QStringLiteral("Kdenlive core/timeline is unavailable."));
        return false;
    }
    TimelineWidget *timelineWidget = pCore->window()->getCurrentTimeline();
    const std::shared_ptr<TimelineItemModel> timeline = timelineWidget ? timelineWidget->model() : nullptr;
    if (!timeline || !timeline->isClip(clipId)) {
        failure = err(QStringLiteral("Clip id %1 does not exist on the active timeline.").arg(clipId));
        return false;
    }
    target.stack = timeline->getClipEffectStack(clipId);
    if (!target.stack || row < 0 || row >= target.stack->rowCount()) {
        failure = err(QStringLiteral("No effect exists at row %1 on clip %2.").arg(row).arg(clipId));
        return false;
    }
    target.effect = std::dynamic_pointer_cast<EffectItemModel>(target.stack->getEffectStackRow(row));
    if (!target.effect) {
        failure = err(QStringLiteral("Effect row %1 is unavailable or is not an editable effect item.").arg(row));
        return false;
    }
    target.keyframes = target.effect->getKeyframeModel();
    if (!target.keyframes) {
        failure = err(QStringLiteral("Effect '%1' at row %2 has no keyframable parameters.").arg(target.effect->getAssetId()).arg(row));
        return false;
    }
    return true;
}

QJsonObject inspect(const QJsonObject &input)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    const int row = input.value(QStringLiteral("row")).toInt(-1);
    Target target;
    QJsonObject failure;
    if (!targetFor(clipId, row, target, failure)) return failure;

    QJsonArray frames;
    const int count = target.keyframes->count();
    for (int i = 0; i < count; ++i) {
        const GenTime pos = target.keyframes->getPosAtIndex(i);
        frames.append(pos.frames(pCore->getCurrentFps()));
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("clip_id"), clipId},
                       {QStringLiteral("row"), row},
                       {QStringLiteral("effect_id"), target.effect->getAssetId()},
                       {QStringLiteral("keyframe_count"), count},
                       {QStringLiteral("clip_relative_frames"), frames}};
}

QJsonObject addKeyframe(const QJsonObject &input)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    const int row = input.value(QStringLiteral("row")).toInt(-1);
    const int frame = input.value(QStringLiteral("frame")).toInt(-1);
    const double value = input.value(QStringLiteral("normalized_value")).toDouble(0.5);
    if (frame < 0) return err(QStringLiteral("frame must be >= 0 and is clip-relative."));
    if (value < 0.0 || value > 1.0) return err(QStringLiteral("normalized_value must be between 0 and 1."));

    Target target;
    QJsonObject failure;
    if (!targetFor(clipId, row, target, failure)) return failure;
    const bool existed = target.keyframes->hasKeyframe(frame);
    if (!target.keyframes->addKeyframe(frame, value)) {
        return err(QStringLiteral("Kdenlive rejected adding/updating the effect keyframe."));
    }
    if (!target.keyframes->hasKeyframe(frame)) {
        return err(QStringLiteral("Effect keyframe operation returned success but frame %1 is not present in the live keyframe model.").arg(frame));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId}, {QStringLiteral("row"), row},
                       {QStringLiteral("effect_id"), target.effect->getAssetId()}, {QStringLiteral("frame"), frame},
                       {QStringLiteral("normalized_value"), value}, {QStringLiteral("updated_existing"), existed},
                       {QStringLiteral("verified"), true}};
}

QJsonObject removeKeyframe(const QJsonObject &input)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    const int row = input.value(QStringLiteral("row")).toInt(-1);
    const int frame = input.value(QStringLiteral("frame")).toInt(-1);
    if (frame < 0) return err(QStringLiteral("frame must be >= 0 and is clip-relative."));
    Target target;
    QJsonObject failure;
    if (!targetFor(clipId, row, target, failure)) return failure;
    if (!target.keyframes->hasKeyframe(frame)) return err(QStringLiteral("No keyframe exists at clip-relative frame %1.").arg(frame));
    const GenTime pos(frame, pCore->getCurrentFps());
    if (!target.keyframes->removeKeyframe(pos)) return err(QStringLiteral("Kdenlive rejected removing the effect keyframe."));
    if (target.keyframes->hasKeyframe(frame)) return err(QStringLiteral("Keyframe removal did not verify on the live effect keyframe model."));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId}, {QStringLiteral("row"), row},
                       {QStringLiteral("effect_id"), target.effect->getAssetId()}, {QStringLiteral("removed_frame"), frame},
                       {QStringLiteral("verified"), true}};
}

QJsonObject moveKeyframe(const QJsonObject &input)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    const int row = input.value(QStringLiteral("row")).toInt(-1);
    const int from = input.value(QStringLiteral("from_frame")).toInt(-1);
    const int to = input.value(QStringLiteral("to_frame")).toInt(-1);
    if (from < 0 || to < 0) return err(QStringLiteral("from_frame and to_frame must be >= 0 and are clip-relative."));
    if (from == to) return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("changed"), false}, {QStringLiteral("verified"), true}};
    Target target;
    QJsonObject failure;
    if (!targetFor(clipId, row, target, failure)) return failure;
    if (!target.keyframes->hasKeyframe(from)) return err(QStringLiteral("No keyframe exists at clip-relative frame %1.").arg(from));
    if (target.keyframes->hasKeyframe(to)) return err(QStringLiteral("A keyframe already exists at destination frame %1; choose another destination or update that keyframe explicitly.").arg(to));
    const GenTime oldPos(from, pCore->getCurrentFps());
    const GenTime newPos(to, pCore->getCurrentFps());
    if (!target.keyframes->moveKeyframe(oldPos, newPos, true, true)) return err(QStringLiteral("Kdenlive rejected moving the effect keyframe."));
    if (target.keyframes->hasKeyframe(from) || !target.keyframes->hasKeyframe(to)) {
        return err(QStringLiteral("Keyframe move did not verify on the live effect keyframe model."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId}, {QStringLiteral("row"), row},
                       {QStringLiteral("effect_id"), target.effect->getAssetId()}, {QStringLiteral("from_frame"), from},
                       {QStringLiteral("to_frame"), to}, {QStringLiteral("changed"), true}, {QStringLiteral("verified"), true}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required}, {QStringLiteral("additionalProperties"), false}};
}
} // namespace

bool registerVibeCutEffectKeyframeTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject targetProps{{QStringLiteral("clip_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                  {QStringLiteral("row"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}}};
    const QJsonObject inspectSchema{{QStringLiteral("name"), QStringLiteral("effect_keyframes_inspect")},
                                    {QStringLiteral("description"), QStringLiteral("Inspect clip-relative keyframe positions for one effect row. Read-only; the tool refuses effects with no keyframable parameters.")},
                                    {QStringLiteral("input_schema"), objectSchema(targetProps, QJsonArray{QStringLiteral("clip_id"), QStringLiteral("row")})}};
    VibeCutToolPolicy inspectPolicy;
    inspectPolicy.name = QStringLiteral("effect_keyframes_inspect");
    inspectPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(inspectSchema, inspectPolicy, inspect, error)) return false;

    QJsonObject addProps = targetProps;
    addProps.insert(QStringLiteral("frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}});
    addProps.insert(QStringLiteral("normalized_value"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}});
    VibeCutToolPolicy editPolicy;
    editPolicy.risk = VibeCutToolRisk::ReversibleEdit;
    editPolicy.reversible = true;
    editPolicy.mutatesProject = true;
    editPolicy.name = QStringLiteral("effect_keyframe_add");
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), editPolicy.name},
                                         {QStringLiteral("description"), QStringLiteral("Add or update an effect keyframe at an explicit clip-relative frame using Kdenlive's native KeyframeModelList. Value is normalized 0..1 across the effect's keyframable parameters.")},
                                         {QStringLiteral("input_schema"), objectSchema(addProps, QJsonArray{QStringLiteral("clip_id"), QStringLiteral("row"), QStringLiteral("frame"), QStringLiteral("normalized_value")})}},
                              editPolicy, addKeyframe, error)) return false;

    QJsonObject removeProps = targetProps;
    removeProps.insert(QStringLiteral("frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}});
    editPolicy.name = QStringLiteral("effect_keyframe_remove");
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), editPolicy.name},
                                         {QStringLiteral("description"), QStringLiteral("Remove an existing effect keyframe at a clip-relative frame through Kdenlive's native undoable keyframe model and verify it is gone.")},
                                         {QStringLiteral("input_schema"), objectSchema(removeProps, QJsonArray{QStringLiteral("clip_id"), QStringLiteral("row"), QStringLiteral("frame")})}},
                              editPolicy, removeKeyframe, error)) return false;

    QJsonObject moveProps = targetProps;
    moveProps.insert(QStringLiteral("from_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}});
    moveProps.insert(QStringLiteral("to_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}});
    editPolicy.name = QStringLiteral("effect_keyframe_move");
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), editPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Move an existing effect keyframe between explicit clip-relative frames through Kdenlive's native undoable keyframe model; refuses an occupied destination and verifies the move.")},
                                            {QStringLiteral("input_schema"), objectSchema(moveProps, QJsonArray{QStringLiteral("clip_id"), QStringLiteral("row"), QStringLiteral("from_frame"), QStringLiteral("to_frame")})}},
                                editPolicy, moveKeyframe, error);
}
