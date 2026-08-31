/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutmixtools.h"

#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "transitions/transitionsrepository.hpp"
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

QString alignmentName(MixAlignment align)
{
    switch (align) {
    case MixAlignment::AlignLeft: return QStringLiteral("left");
    case MixAlignment::AlignRight: return QStringLiteral("right");
    case MixAlignment::AlignCenter: return QStringLiteral("center");
    case MixAlignment::AlignNone: break;
    }
    return QStringLiteral("none");
}

MixAlignment parseAlignment(const QString &value, bool *ok)
{
    if (ok) *ok = true;
    if (value == QLatin1String("left")) return MixAlignment::AlignLeft;
    if (value == QLatin1String("right")) return MixAlignment::AlignRight;
    if (value == QLatin1String("center")) return MixAlignment::AlignCenter;
    if (value == QLatin1String("none")) return MixAlignment::AlignNone;
    if (ok) *ok = false;
    return MixAlignment::AlignNone;
}

QJsonObject inspectMix(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    if (!model->isClip(clipId)) return err(QStringLiteral("Clip id %1 does not exist.").arg(clipId));
    const int duration = model->getMixDuration(clipId);
    const bool exists = duration > 0;
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId},
                       {QStringLiteral("has_mix"), exists}, {QStringLiteral("duration_frames"), duration}};
    if (exists) {
        result.insert(QStringLiteral("cut_position_frames"), model->getMixCutPos(clipId));
        result.insert(QStringLiteral("alignment"), alignmentName(model->getMixAlign(clipId)));
        const std::pair<int, int> inOut = model->getMixInOut(clipId);
        result.insert(QStringLiteral("in_frame"), inOut.first);
        result.insert(QStringLiteral("out_frame"), inOut.second);
    }
    return result;
}

QJsonObject addPreviousMix(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    const QString transitionId = input.value(QStringLiteral("transition_id")).toString(QStringLiteral("luma")).trimmed();
    if (!model->isClip(clipId)) return err(QStringLiteral("Clip id %1 does not exist.").arg(clipId));
    if (model->getMixDuration(clipId) > 0) return err(QStringLiteral("Clip %1 already owns a mix. Inspect/remove it first.").arg(clipId));
    if (!TransitionsRepository::get()->exists(transitionId)) {
        return err(QStringLiteral("Unknown installed transition id '%1'. Call transitions_list first.").arg(transitionId));
    }
    if (!model->mixClip(clipId, transitionId, -1)) {
        return err(QStringLiteral("Kdenlive could not create a same-track mix between clip %1 and its previous neighbor.").arg(clipId));
    }
    const int duration = model->getMixDuration(clipId);
    if (duration <= 0) {
        return err(QStringLiteral("Mix creation returned success but no live mix was found on clip %1.").arg(clipId));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId},
                       {QStringLiteral("transition_id"), transitionId}, {QStringLiteral("duration_frames"), duration},
                       {QStringLiteral("cut_position_frames"), model->getMixCutPos(clipId)},
                       {QStringLiteral("alignment"), alignmentName(model->getMixAlign(clipId))},
                       {QStringLiteral("verified"), true}};
}

QJsonObject resizeMix(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    const int duration = input.value(QStringLiteral("duration_frames")).toInt(-1);
    const QString alignText = input.value(QStringLiteral("alignment")).toString(QStringLiteral("none"));
    const int leftFrames = input.contains(QStringLiteral("left_frames")) ? input.value(QStringLiteral("left_frames")).toInt(-1) : -1;
    if (!model->isClip(clipId)) return err(QStringLiteral("Clip id %1 does not exist.").arg(clipId));
    if (duration <= 0) return err(QStringLiteral("duration_frames must be > 0"));
    if (model->getMixDuration(clipId) <= 0) return err(QStringLiteral("Clip %1 has no mix to resize.").arg(clipId));
    bool alignOk = false;
    const MixAlignment align = parseAlignment(alignText, &alignOk);
    if (!alignOk) return err(QStringLiteral("alignment must be one of none, left, right, center"));
    const int oldDuration = model->getMixDuration(clipId);
    const MixAlignment oldAlign = model->getMixAlign(clipId);
    model->requestResizeMix(clipId, duration, align, leftFrames);
    const int liveDuration = model->getMixDuration(clipId);
    const MixAlignment liveAlign = model->getMixAlign(clipId);
    if (liveDuration <= 0) return err(QStringLiteral("Mix resize removed or invalidated the live mix unexpectedly."));
    if (liveDuration == oldDuration && liveAlign == oldAlign && (duration != oldDuration || align != oldAlign)) {
        return err(QStringLiteral("Kdenlive did not apply the requested mix resize/alignment."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId},
                       {QStringLiteral("old_duration_frames"), oldDuration}, {QStringLiteral("duration_frames"), liveDuration},
                       {QStringLiteral("old_alignment"), alignmentName(oldAlign)}, {QStringLiteral("alignment"), alignmentName(liveAlign)},
                       {QStringLiteral("cut_position_frames"), model->getMixCutPos(clipId)},
                       {QStringLiteral("verified"), true}};
}

QJsonObject removeMix(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    const int clipId = input.value(QStringLiteral("clip_id")).toInt(-1);
    if (!model->isClip(clipId)) return err(QStringLiteral("Clip id %1 does not exist.").arg(clipId));
    const int oldDuration = model->getMixDuration(clipId);
    if (oldDuration <= 0) return err(QStringLiteral("Clip %1 has no mix to remove.").arg(clipId));
    if (!model->removeMix(clipId)) return err(QStringLiteral("Kdenlive rejected removing the mix on clip %1.").arg(clipId));
    if (model->getMixDuration(clipId) > 0) return err(QStringLiteral("Mix removal returned success but the mix is still present."));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), clipId},
                       {QStringLiteral("removed_duration_frames"), oldDuration}, {QStringLiteral("verified"), true}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required}, {QStringLiteral("additionalProperties"), false}};
}

bool registerTool(VibeCutToolSurface &surface, const QString &name, const QString &description, const QJsonObject &input,
                  VibeCutToolRisk risk, bool mutates, const VibeCutToolSurface::Handler &handler, QString *error)
{
    VibeCutToolPolicy policy;
    policy.name = name;
    policy.risk = risk;
    policy.reversible = mutates;
    policy.mutatesProject = mutates;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("description"), description},
                                            {QStringLiteral("input_schema"), input}}, policy, handler, error);
}
} // namespace

bool registerVibeCutMixTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject clipInput = objectSchema(QJsonObject{{QStringLiteral("clip_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}},
                                                QJsonArray{QStringLiteral("clip_id")});
    if (!registerTool(surface, QStringLiteral("mix_inspect"),
                      QStringLiteral("Inspect the same-track mix owned by a right-hand/second clip, including duration, cut position and alignment. Read-only."),
                      clipInput, VibeCutToolRisk::ReadOnly, false, inspectMix, error)) return false;

    const QJsonObject addInput = objectSchema(QJsonObject{
        {QStringLiteral("clip_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                {QStringLiteral("description"), QStringLiteral("Right-hand clip; VibeCut mixes it with its previous neighbor on the same track.")}}},
        {QStringLiteral("transition_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                      {QStringLiteral("description"), QStringLiteral("Installed transition id; defaults to luma.")}}}},
        QJsonArray{QStringLiteral("clip_id")});
    if (!registerTool(surface, QStringLiteral("mix_add_previous"),
                      QStringLiteral("Create an undoable Kdenlive same-track mix between this clip and its previous neighbor; verify the resulting mix on the supplied right-hand clip."),
                      addInput, VibeCutToolRisk::ReversibleEdit, true, addPreviousMix, error)) return false;

    const QJsonObject resizeInput = objectSchema(QJsonObject{
        {QStringLiteral("clip_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
        {QStringLiteral("duration_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}},
        {QStringLiteral("alignment"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("none"), QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("center")}}}},
        {QStringLiteral("left_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), -1}}}},
        QJsonArray{QStringLiteral("clip_id"), QStringLiteral("duration_frames")});
    if (!registerTool(surface, QStringLiteral("mix_resize"),
                      QStringLiteral("Resize/re-align an existing same-track mix through Kdenlive's native requestResizeMix path, which creates undo/redo and is live-state verified."),
                      resizeInput, VibeCutToolRisk::ReversibleEdit, true, resizeMix, error)) return false;

    return registerTool(surface, QStringLiteral("mix_remove"),
                        QStringLiteral("Remove an existing same-track mix using Kdenlive's native undoable removeMix path and verify it is gone."),
                        clipInput, VibeCutToolRisk::ReversibleEdit, true, removeMix, error);
}
