/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuttakeselectiontools.h"

#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <QHash>
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

QJsonObject buildSelectionPlan(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface) return err(QStringLiteral("VibeCut tool surface is unavailable."));
    const std::shared_ptr<TimelineItemModel> timeline = currentModel();
    if (!timeline) return err(QStringLiteral("No active timeline is open."));

    QJsonObject reviewInput;
    const QStringList passthrough{QStringLiteral("min_words"), QStringLiteral("similarity_threshold"),
                                  QStringLiteral("max_segments"), QStringLiteral("max_groups")};
    for (const QString &name : passthrough) if (input.contains(name)) reviewInput.insert(name, input.value(name));
    const QJsonObject review = surface->invoke(QStringLiteral("repeated_take_review"), reviewInput);
    if (!review.value(QStringLiteral("ok")).toBool(false)) return review;

    QHash<int, int> keepByGroup;
    const QJsonArray selections = input.value(QStringLiteral("selections")).toArray();
    if (selections.isEmpty()) return err(QStringLiteral("selections must contain at least one explicit group_index/keep_subtitle_id choice."));
    for (const QJsonValue &value : selections) {
        const QJsonObject selection = value.toObject();
        const int groupIndex = selection.value(QStringLiteral("group_index")).toInt(-1);
        const int keepSubtitle = selection.value(QStringLiteral("keep_subtitle_id")).toInt(-1);
        if (groupIndex < 0 || keepSubtitle < 0) return err(QStringLiteral("Each selection requires non-negative group_index and keep_subtitle_id."));
        if (keepByGroup.contains(groupIndex)) return err(QStringLiteral("Group %1 appears more than once in selections.").arg(groupIndex));
        keepByGroup.insert(groupIndex, keepSubtitle);
    }

    QJsonArray resolvedGroups;
    QSet<int> seenGroups;
    int rejectedCount = 0;
    for (const QJsonValue &groupValue : review.value(QStringLiteral("groups")).toArray()) {
        const QJsonObject group = groupValue.toObject();
        const int groupIndex = group.value(QStringLiteral("group_index")).toInt(-1);
        if (!keepByGroup.contains(groupIndex)) continue;
        seenGroups.insert(groupIndex);
        const int keepSubtitle = keepByGroup.value(groupIndex);
        bool keepFound = false;
        QJsonObject kept;
        QJsonArray rejected;

        for (const QJsonValue &takeValue : group.value(QStringLiteral("takes")).toArray()) {
            const QJsonObject take = takeValue.toObject();
            const int subtitleId = take.value(QStringLiteral("subtitle_id")).toInt(-1);
            if (subtitleId == keepSubtitle) {
                keepFound = true;
                kept = take;
                continue;
            }
            const int start = take.value(QStringLiteral("start_frame")).toInt(-1);
            const int end = take.value(QStringLiteral("end_frame")).toInt(-1);
            if (start < 0 || end <= start) return err(QStringLiteral("Repeated-take review returned an invalid take range."));

            QJsonArray liveClips;
            bool grouped = false;
            const int midpoint = start + (end - start) / 2;
            for (int trackId : timeline->getAllTracksIds()) {
                const int clipId = timeline->getClipByPosition(trackId, midpoint);
                if (clipId < 0 || !timeline->isClip(clipId)) continue;
                const bool clipGrouped = timeline->isInGroup(clipId);
                grouped = grouped || clipGrouped;
                liveClips.append(QJsonObject{{QStringLiteral("clip_id"), clipId},
                                             {QStringLiteral("track_id"), trackId},
                                             {QStringLiteral("bin_id"), timeline->getClipBinId(clipId)},
                                             {QStringLiteral("grouped"), clipGrouped},
                                             {QStringLiteral("clip_position_frame"), timeline->getClipPosition(clipId)},
                                             {QStringLiteral("source_in_frame"), timeline->getClipIn(clipId)}});
            }
            QJsonObject reject = take;
            reject.insert(QStringLiteral("timeline_start_frame"), start);
            reject.insert(QStringLiteral("timeline_end_frame"), end);
            reject.insert(QStringLiteral("remove_frames"), end - start);
            reject.insert(QStringLiteral("live_clips"), liveClips);
            reject.insert(QStringLiteral("grouped"), grouped);
            reject.insert(QStringLiteral("execution_ready"), false);
            rejected.append(reject);
            ++rejectedCount;
        }
        if (!keepFound) {
            return err(QStringLiteral("keep_subtitle_id %1 is not a member of repeated-take group %2.").arg(keepSubtitle).arg(groupIndex));
        }
        resolvedGroups.append(QJsonObject{{QStringLiteral("group_index"), groupIndex},
                                          {QStringLiteral("similarity"), group.value(QStringLiteral("similarity"))},
                                          {QStringLiteral("keep"), kept},
                                          {QStringLiteral("reject"), rejected},
                                          {QStringLiteral("reject_count"), rejected.size()}});
    }

    for (auto it = keepByGroup.constBegin(); it != keepByGroup.constEnd(); ++it) {
        if (!seenGroups.contains(it.key())) return err(QStringLiteral("Requested repeated-take group %1 does not exist under the current review parameters.").arg(it.key()));
    }

    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("selection_count"), selections.size()},
                       {QStringLiteral("resolved_group_count"), resolvedGroups.size()},
                       {QStringLiteral("rejected_take_count"), rejectedCount},
                       {QStringLiteral("groups"), resolvedGroups},
                       {QStringLiteral("execution_ready"), false},
                       {QStringLiteral("note"), QStringLiteral("Explicit keep choices are validated against the current repeated-take review. Rejected ranges are exact transcript/timeline spans only; execution remains separate so linked/group topology and desired lift/ripple behavior can be authorized explicitly.")}};
}
} // namespace

bool registerVibeCutTakeSelectionTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject selectionItem{{QStringLiteral("type"), QStringLiteral("object")},
                                    {QStringLiteral("properties"), QJsonObject{
                                        {QStringLiteral("group_index"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                        {QStringLiteral("keep_subtitle_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}}}},
                                    {QStringLiteral("required"), QJsonArray{QStringLiteral("group_index"), QStringLiteral("keep_subtitle_id")}},
                                    {QStringLiteral("additionalProperties"), false}};
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("selections"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("minItems"), 1}, {QStringLiteral("maxItems"), 500}, {QStringLiteral("items"), selectionItem}}},
                                {QStringLiteral("min_words"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 3}, {QStringLiteral("maximum"), 100}}},
                                {QStringLiteral("similarity_threshold"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.50}, {QStringLiteral("maximum"), 1.0}}},
                                {QStringLiteral("max_segments"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 2}, {QStringLiteral("maximum"), 1000}}},
                                {QStringLiteral("max_groups"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 500}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("selections")}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("repeated_take_selection_plan");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Validate explicit keep-take selections against the current repeated_take_review and produce exact rejected transcript/timeline ranges with live clip/group context. The caller chooses taste; VibeCut does not infer a winner and does not mutate the project.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, [&surface](const QJsonObject &input) { return buildSelectionPlan(&surface, input); }, error);
}
