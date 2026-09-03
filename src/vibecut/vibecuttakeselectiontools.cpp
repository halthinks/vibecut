/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuttakeselectiontools.h"

#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecutedittools.h"
#include "vibecuttoolsurface.h"

#include <KLocalizedString>
#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <vector>

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
            reject.insert(QStringLiteral("execution_ready"), true);
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
                       {QStringLiteral("execution_ready"), rejectedCount > 0},
                       {QStringLiteral("note"), QStringLiteral("Explicit keep choices are validated against the current repeated-take review. Rejected ranges are exact transcript/timeline spans. Execution must explicitly choose lift or ripple semantics and reuses the governed timeline_range_remove transaction path.")}};
}

struct RemovalRange {
    int start = -1;
    int end = -1;
    int groupIndex = -1;
    int subtitleId = -1;
};

QJsonObject executeSelection(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface) return err(QStringLiteral("VibeCut tool surface is unavailable."));
    const QString mode = input.value(QStringLiteral("remove_mode")).toString();
    if (mode != QLatin1String("lift") && mode != QLatin1String("ripple")) return err(QStringLiteral("remove_mode must be 'lift' or 'ripple'."));

    const std::shared_ptr<TimelineItemModel> timeline = currentModel();
    if (!timeline) return err(QStringLiteral("No active timeline is open."));

    QJsonObject planInput = input;
    planInput.remove(QStringLiteral("remove_mode"));
    const QJsonObject plan = buildSelectionPlan(surface, planInput);
    if (!plan.value(QStringLiteral("ok")).toBool(false)) return plan;
    return executeVibeCutResolvedTakeSelection(timeline, plan, mode);
}

QJsonObject selectionInputSchema(bool execution)
{
    const QJsonObject selectionItem{{QStringLiteral("type"), QStringLiteral("object")},
                                    {QStringLiteral("properties"), QJsonObject{
                                        {QStringLiteral("group_index"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                        {QStringLiteral("keep_subtitle_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}}}},
                                    {QStringLiteral("required"), QJsonArray{QStringLiteral("group_index"), QStringLiteral("keep_subtitle_id")}},
                                    {QStringLiteral("additionalProperties"), false}};
    QJsonObject properties{
        {QStringLiteral("selections"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("minItems"), 1}, {QStringLiteral("maxItems"), 500}, {QStringLiteral("items"), selectionItem}}},
        {QStringLiteral("min_words"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 3}, {QStringLiteral("maximum"), 100}}},
        {QStringLiteral("similarity_threshold"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.50}, {QStringLiteral("maximum"), 1.0}}},
        {QStringLiteral("max_segments"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 2}, {QStringLiteral("maximum"), 1000}}},
        {QStringLiteral("max_groups"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 500}}}};
    QJsonArray required{QStringLiteral("selections")};
    if (execution) {
        properties.insert(QStringLiteral("remove_mode"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                      {QStringLiteral("enum"), QJsonArray{QStringLiteral("lift"), QStringLiteral("ripple")}}});
        required.append(QStringLiteral("remove_mode"));
    }
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required},
                       {QStringLiteral("additionalProperties"), false}};
}
} // namespace

QJsonObject executeVibeCutResolvedTakeSelection(const std::shared_ptr<TimelineItemModel> &timeline,
                                                const QJsonObject &selectionPlan,
                                                const QString &removeMode)
{
    if (!timeline) return err(QStringLiteral("No authoritative timeline model was provided for repeated-take execution."));
    if (removeMode != QLatin1String("lift") && removeMode != QLatin1String("ripple")) {
        return err(QStringLiteral("remove_mode must be 'lift' or 'ripple'."));
    }
    if (!selectionPlan.value(QStringLiteral("ok")).toBool(false)) {
        return err(QStringLiteral("Resolved repeated-take selection plan is not marked ok; refusing mutation."));
    }

    std::vector<RemovalRange> ranges;
    for (const QJsonValue &groupValue : selectionPlan.value(QStringLiteral("groups")).toArray()) {
        const QJsonObject group = groupValue.toObject();
        const int groupIndex = group.value(QStringLiteral("group_index")).toInt(-1);
        for (const QJsonValue &rejectValue : group.value(QStringLiteral("reject")).toArray()) {
            const QJsonObject reject = rejectValue.toObject();
            const RemovalRange range{reject.value(QStringLiteral("timeline_start_frame")).toInt(-1),
                                     reject.value(QStringLiteral("timeline_end_frame")).toInt(-1),
                                     groupIndex,
                                     reject.value(QStringLiteral("subtitle_id")).toInt(-1)};
            if (range.start < 0 || range.end <= range.start) {
                return err(QStringLiteral("Resolved repeated-take selection contains invalid rejected range [%1,%2).")
                               .arg(range.start).arg(range.end));
            }
            ranges.push_back(range);
        }
    }
    if (ranges.empty()) return err(QStringLiteral("The explicit repeated-take selection does not reject any take; nothing to execute."));

    std::sort(ranges.begin(), ranges.end(), [](const RemovalRange &a, const RemovalRange &b) {
        if (a.start != b.start) return a.start < b.start;
        return a.end < b.end;
    });
    for (size_t i = 1; i < ranges.size(); ++i) {
        if (ranges[i].start < ranges[i - 1].end) {
            return err(QStringLiteral("Rejected repeated-take ranges overlap ([%1,%2) and [%3,%4)); refusing ambiguous mutation.")
                           .arg(ranges[i - 1].start).arg(ranges[i - 1].end).arg(ranges[i].start).arg(ranges[i].end));
        }
    }

    // Execute from right to left so ripple extraction cannot invalidate the
    // absolute coordinates of earlier rejected takes.
    std::sort(ranges.begin(), ranges.end(), [](const RemovalRange &a, const RemovalRange &b) { return a.start > b.start; });

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QJsonArray operations;
    for (const RemovalRange &range : ranges) {
        QJsonObject verification;
        QString failure;
        if (!appendVibeCutTimelineRangeRemove(timeline, range.start, range.end, removeMode == QLatin1String("lift"), {}, undo, redo, &verification, &failure)) {
            const bool rollbackOk = undo();
            return err(QStringLiteral("Repeated-take execution rolled back after range [%1,%2) failed: %3%4")
                           .arg(range.start).arg(range.end).arg(failure)
                           .arg(rollbackOk ? QString() : QStringLiteral("; accumulated rollback also reported failure")));
        }
        verification.insert(QStringLiteral("group_index"), range.groupIndex);
        verification.insert(QStringLiteral("subtitle_id"), range.subtitleId);
        verification.insert(QStringLiteral("start_frame"), range.start);
        verification.insert(QStringLiteral("end_frame"), range.end);
        operations.append(verification);
    }

    if (!pCore) {
        const bool rollbackOk = undo();
        return err(rollbackOk ? QStringLiteral("VibeCut core is unavailable; repeated-take mutation was rolled back before commit.")
                              : QStringLiteral("VibeCut core is unavailable and accumulated rollback also reported failure."));
    }
    pCore->pushUndo(undo, redo, i18n("Apply VibeCut repeated-take selection"));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("verified"), true},
                       {QStringLiteral("remove_mode"), removeMode},
                       {QStringLiteral("removed_take_count"), static_cast<int>(ranges.size())},
                       {QStringLiteral("operations"), operations},
                       {QStringLiteral("selection_plan"), selectionPlan},
                       {QStringLiteral("undo_atomic"), true},
                       {QStringLiteral("note"), QStringLiteral("The explicit keep choices were resolved before mutation. Production execution revalidates them immediately beforehand. Rejected ranges were applied right-to-left through Kdenlive's native accumulated zone extraction and committed as one Undo step.")}};
}

bool registerVibeCutTakeSelectionTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutToolPolicy planPolicy;
    planPolicy.name = QStringLiteral("repeated_take_selection_plan");
    planPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), planPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Validate explicit keep-take selections against the current repeated_take_review and produce exact rejected transcript/timeline ranges with live clip/group context. The caller chooses taste; VibeCut does not infer a winner or mutate the project while planning.")},
                                          {QStringLiteral("input_schema"), selectionInputSchema(false)}},
                              planPolicy, [&surface](const QJsonObject &input) { return buildSelectionPlan(&surface, input); }, error)) return false;

    VibeCutToolPolicy executePolicy;
    executePolicy.name = QStringLiteral("repeated_take_selection_execute");
    executePolicy.risk = VibeCutToolRisk::MajorEdit;
    executePolicy.reversible = true;
    executePolicy.mutatesProject = true;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), executePolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Execute explicit repeated-take keep choices after a fresh review. Requires explicit lift/ripple semantics, rejects overlapping ranges, removes rejected takes right-to-left through the governed native timeline range-removal path, verifies every range, rolls back on failure, and commits the successful batch as one Undo step.")},
                                            {QStringLiteral("input_schema"), selectionInputSchema(true)}},
                                executePolicy, [&surface](const QJsonObject &input) { return executeSelection(&surface, input); }, error);
}
