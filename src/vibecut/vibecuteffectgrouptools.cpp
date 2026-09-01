/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuteffectgrouptools.h"

#include "core.h"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "effects/effectsrepository.hpp"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <QDomElement>
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

QStringList groupChildIds(const QDomElement &groupXml)
{
    QStringList ids;
    for (QDomNode node = groupXml.firstChild(); !node.isNull(); node = node.nextSibling()) {
        const QDomElement child = node.toElement();
        if (child.isNull() || child.tagName() != QLatin1String("effect")) continue;
        const QString id = child.attribute(QStringLiteral("id")).trimmed();
        if (!id.isEmpty()) ids.append(id);
    }
    return ids;
}

QJsonObject inspectGroup(const QJsonObject &input)
{
    const QString groupId = input.value(QStringLiteral("group_id")).toString().trimmed();
    if (groupId.isEmpty()) return err(QStringLiteral("group_id must not be empty"));
    if (!EffectsRepository::get()->exists(groupId) || !EffectsRepository::get()->isGroup(groupId)) {
        return err(QStringLiteral("'%1' is not an installed Kdenlive effect group.").arg(groupId));
    }
    const QDomElement xml = EffectsRepository::get()->getXml(groupId);
    const QStringList children = groupChildIds(xml);
    QJsonArray childIds;
    for (const QString &id : children) childIds.append(id);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("group_id"), groupId},
                       {QStringLiteral("name"), EffectsRepository::get()->getName(groupId)},
                       {QStringLiteral("description"), EffectsRepository::get()->getDescription(groupId)},
                       {QStringLiteral("child_count"), children.size()},
                       {QStringLiteral("child_effect_ids"), childIds}};
}

QJsonObject addGroup(const QJsonObject &input)
{
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    const QString groupId = input.value(QStringLiteral("group_id")).toString().trimmed();
    if (groupId.isEmpty()) return err(QStringLiteral("group_id must not be empty"));
    if (!EffectsRepository::get()->exists(groupId) || !EffectsRepository::get()->isGroup(groupId)) {
        return err(QStringLiteral("'%1' is not an installed Kdenlive effect group. Call effects_available and select an entry with group=true.").arg(groupId));
    }

    const QDomElement groupXml = EffectsRepository::get()->getXml(groupId);
    const QStringList expected = groupChildIds(groupXml);
    if (expected.isEmpty()) return err(QStringLiteral("Effect group '%1' contains no direct child effects to apply.").arg(groupId));

    QJsonObject failure;
    const std::shared_ptr<EffectStackModel> stack = clipStack(clipId, failure);
    if (!stack) return failure;

    const int before = stack->rowCount();
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (!stack->copyXmlEffectWithUndo(groupXml, undo, redo)) {
        return err(QStringLiteral("Kdenlive rejected effect group '%1' on clip %2.").arg(groupId).arg(clipId));
    }

    const int after = stack->rowCount();
    if (after != before + expected.size()) {
        undo();
        return err(QStringLiteral("Effect group verification failed: expected %1 child rows but stack changed from %2 to %3.")
                       .arg(expected.size()).arg(before).arg(after));
    }

    QJsonArray rows;
    for (int i = 0; i < expected.size(); ++i) {
        const int row = before + i;
        const std::shared_ptr<EffectItemModel> effect = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (!effect || effect->getAssetId() != expected.at(i)) {
            const QString liveId = effect ? effect->getAssetId() : QStringLiteral("<unavailable>");
            undo();
            return err(QStringLiteral("Effect group verification failed at child %1: expected '%2', live row %3 is '%4'.")
                           .arg(i).arg(expected.at(i)).arg(row).arg(liveId));
        }
        rows.append(QJsonObject{{QStringLiteral("row"), row}, {QStringLiteral("effect_id"), effect->getAssetId()},
                                {QStringLiteral("effect_name"), EffectsRepository::get()->getName(effect->getAssetId())}});
    }

    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: add effect group %1").arg(EffectsRepository::get()->getName(groupId)));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("clip_id"), clipId},
                       {QStringLiteral("group_id"), groupId},
                       {QStringLiteral("group_name"), EffectsRepository::get()->getName(groupId)},
                       {QStringLiteral("child_count"), expected.size()},
                       {QStringLiteral("children"), rows},
                       {QStringLiteral("verified"), true}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required},
                       {QStringLiteral("additionalProperties"), false}};
}
} // namespace

bool registerVibeCutEffectGroupTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject inspectInput = objectSchema(
        QJsonObject{{QStringLiteral("group_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}},
        QJsonArray{QStringLiteral("group_id")});
    const QJsonObject inspectSchema{{QStringLiteral("name"), QStringLiteral("effect_group_inspect")},
                                    {QStringLiteral("description"), QStringLiteral("Inspect one installed Kdenlive effect group's direct child effect ids before application. Read-only; use this to understand exactly what a saved effect preset/group will add." )},
                                    {QStringLiteral("input_schema"), inspectInput}};
    VibeCutToolPolicy inspectPolicy;
    inspectPolicy.name = QStringLiteral("effect_group_inspect");
    inspectPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(inspectSchema, inspectPolicy, inspectGroup, error)) return false;

    const QJsonObject addInput = objectSchema(
        QJsonObject{{QStringLiteral("clip_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                    {QStringLiteral("group_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}},
        QJsonArray{QStringLiteral("clip_id"), QStringLiteral("group_id")});
    const QJsonObject addSchema{{QStringLiteral("name"), QStringLiteral("effect_group_add")},
                                {QStringLiteral("description"), QStringLiteral("Apply one installed Kdenlive effect group/preset using Kdenlive's native XML import path, then verify every expected child effect id and row before committing one undo record. Verification failure rolls the group back." )},
                                {QStringLiteral("input_schema"), addInput}};
    VibeCutToolPolicy addPolicy;
    addPolicy.name = QStringLiteral("effect_group_add");
    addPolicy.risk = VibeCutToolRisk::ReversibleEdit;
    addPolicy.reversible = true;
    addPolicy.mutatesProject = true;
    return surface.registerTool(addSchema, addPolicy, addGroup, error);
}
