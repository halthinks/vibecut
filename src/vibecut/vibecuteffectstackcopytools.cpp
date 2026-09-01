/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuteffectstackcopytools.h"

#include "core.h"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <QDomDocument>
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

QStringList nonBuiltInIds(const std::shared_ptr<EffectStackModel> &stack)
{
    QStringList ids;
    if (!stack) return ids;
    for (int row = 0; row < stack->rowCount(); ++row) {
        const auto item = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (item && !item->isBuiltIn()) ids.append(item->getAssetId());
    }
    return ids;
}

QDomDocument exportUserEffects(const std::shared_ptr<EffectStackModel> &stack, QStringList &expectedIds)
{
    QDomDocument document;
    QDomElement root = document.createElement(QStringLiteral("effects"));
    QDomDocument full;
    const QDomElement fullRoot = stack->toXml(full);
    root.setAttribute(QStringLiteral("parentIn"), fullRoot.attribute(QStringLiteral("parentIn")));
    document.appendChild(root);

    for (int row = 0; row < stack->rowCount(); ++row) {
        const auto item = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (!item || item->isBuiltIn()) continue;
        QDomDocument rowDoc;
        const QDomElement rowRoot = stack->rowToXml(row, rowDoc);
        const QDomElement effect = rowRoot.firstChildElement(QStringLiteral("effect"));
        if (effect.isNull()) continue;
        expectedIds.append(item->getAssetId());
        root.appendChild(document.importNode(effect, true));
    }
    return document;
}

bool endsWith(const QStringList &haystack, const QStringList &needle)
{
    if (needle.size() > haystack.size()) return false;
    const int offset = haystack.size() - needle.size();
    for (int i = 0; i < needle.size(); ++i) {
        if (haystack.at(offset + i) != needle.at(i)) return false;
    }
    return true;
}

QJsonObject copyStack(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));

    const int sourceId = input.value(QStringLiteral("source_clip_id")).toInt(-1);
    if (!model->isClip(sourceId)) return err(QStringLiteral("source_clip_id %1 is not a live timeline clip.").arg(sourceId));
    const std::shared_ptr<EffectStackModel> sourceStack = model->getClipEffectStackModel(sourceId);
    if (!sourceStack) return err(QStringLiteral("Source clip effect stack is unavailable."));

    QStringList expectedIds;
    QDomDocument payload = exportUserEffects(sourceStack, expectedIds);
    if (expectedIds.isEmpty()) return err(QStringLiteral("Source clip has no non-built-in effects to copy."));

    std::unordered_set<int> targets;
    if (input.contains(QStringLiteral("target_clip_ids"))) {
        const QJsonArray values = input.value(QStringLiteral("target_clip_ids")).toArray();
        if (values.isEmpty()) return err(QStringLiteral("target_clip_ids must not be empty when supplied."));
        for (const QJsonValue &value : values) {
            const int id = value.toInt(-1);
            if (!model->isClip(id)) return err(QStringLiteral("Target id %1 is not a live timeline clip.").arg(id));
            if (id == sourceId) return err(QStringLiteral("Source clip cannot also be a target."));
            if (!targets.insert(id).second) return err(QStringLiteral("target_clip_ids contains duplicate id %1.").arg(id));
        }
    } else {
        for (int id : model->getCurrentSelection()) {
            if (id == sourceId) continue;
            if (!model->isClip(id)) return err(QStringLiteral("Current selection contains non-clip item %1.").arg(id));
            targets.insert(id);
        }
    }
    if (targets.empty()) return err(QStringLiteral("No target clips were supplied or selected."));
    if (targets.size() > 100) return err(QStringLiteral("Effect-stack copy is limited to 100 targets per governed operation."));

    const bool replaceExisting = input.value(QStringLiteral("replace_existing")).toBool(false);
    QJsonArray targetPreview;
    for (int id : targets) {
        targetPreview.append(QJsonObject{{QStringLiteral("clip_id"), id},
                                         {QStringLiteral("existing_effect_ids"), QJsonArray::fromStringList(nonBuiltInIds(model->getClipEffectStackModel(id)))}});
    }
    if (input.value(QStringLiteral("dry_run")).toBool(false)) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("dry_run"), true},
                           {QStringLiteral("source_clip_id"), sourceId},
                           {QStringLiteral("source_effect_ids"), QJsonArray::fromStringList(expectedIds)},
                           {QStringLiteral("replace_existing"), replaceExisting},
                           {QStringLiteral("targets"), targetPreview}};
    }

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    const QDomElement payloadRoot = payload.documentElement();

    for (int id : targets) {
        const std::shared_ptr<EffectStackModel> dest = model->getClipEffectStackModel(id);
        if (!dest) {
            undo();
            return err(QStringLiteral("Effect stack for target clip %1 is unavailable; operation rolled back.").arg(id));
        }
        if (replaceExisting) dest->removeAllEffects(undo, redo);
        const QStringList beforeIds = nonBuiltInIds(dest);
        if (!dest->fromXml(payloadRoot, undo, redo)) {
            undo();
            return err(QStringLiteral("Kdenlive rejected effect-stack import for clip %1; operation rolled back.").arg(id));
        }
        const QStringList liveIds = nonBuiltInIds(dest);
        const bool verified = replaceExisting ? liveIds == expectedIds : endsWith(liveIds, expectedIds) && liveIds.size() == beforeIds.size() + expectedIds.size();
        if (!verified) {
            undo();
            return err(QStringLiteral("Effect-stack copy verification failed on clip %1; operation rolled back.").arg(id));
        }
    }

    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: copy effect stack to %1 clips").arg(targets.size()));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("source_clip_id"), sourceId},
                       {QStringLiteral("source_effect_ids"), QJsonArray::fromStringList(expectedIds)},
                       {QStringLiteral("target_count"), static_cast<int>(targets.size())},
                       {QStringLiteral("replace_existing"), replaceExisting}, {QStringLiteral("verified"), true}};
}
} // namespace

bool registerVibeCutEffectStackCopyTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("source_clip_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                {QStringLiteral("target_clip_ids"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                                {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                                                                {QStringLiteral("maxItems"), 100}}},
                                {QStringLiteral("replace_existing"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
                                {QStringLiteral("dry_run"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("source_clip_id")}},
                            {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("effect_stack_copy_to")},
                             {QStringLiteral("description"), QStringLiteral("Copy all non-built-in effects, including serialized parameters/keyframes, from one timeline clip to explicit or selected target clips through Kdenlive's undoable XML importer. Can append or replace non-built-in target effects, supports dry-run, verifies exact copied effect ids on every target, and rolls back the whole batch on mismatch.")},
                             {QStringLiteral("input_schema"), input}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("effect_stack_copy_to");
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    return surface.registerTool(schema, policy, copyStack, error);
}
