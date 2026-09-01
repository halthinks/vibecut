/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutdeadlinkedtools.h"

#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelinefunctions.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <unordered_set>
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

QSet<int> trackSet(const QVector<int> &tracks)
{
    QSet<int> result;
    for (int track : tracks) result.insert(track);
    return result;
}

QString groupRangeKey(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, int start, int end)
{
    std::vector<int> leaves;
    const std::unordered_set<int> group = timeline->getGroupElements(clipId);
    leaves.assign(group.begin(), group.end());
    std::sort(leaves.begin(), leaves.end());
    QStringList ids;
    for (int id : leaves) ids.append(QString::number(id));
    return QStringLiteral("%1@%2:%3").arg(ids.join(QLatin1Char(','))).arg(start).arg(end);
}

bool groupTracksForRange(const std::shared_ptr<TimelineItemModel> &timeline, int anchorId, int start, int end,
                         QVector<int> &tracks, QString &error)
{
    tracks.clear();
    QSet<int> uniqueTracks;
    const std::unordered_set<int> leaves = timeline->getGroupElements(anchorId);
    if (leaves.empty()) {
        error = QStringLiteral("Could not resolve the live group leaves for clip %1.").arg(anchorId);
        return false;
    }
    for (int id : leaves) {
        if (!timeline->isClip(id)) {
            error = QStringLiteral("Linked dead-air cleanup currently supports clip-only groups; group leaf %1 is not a timeline clip.").arg(id);
            return false;
        }
        const int pos = timeline->getClipPosition(id);
        const int itemEnd = pos + timeline->getClipPlaytime(id);
        if (start < pos || end > itemEnd) {
            error = QStringLiteral("Group leaf %1 does not span the requested dead-air range [%2,%3).").arg(id).arg(start).arg(end);
            return false;
        }
        uniqueTracks.insert(timeline->getClipTrackId(id));
    }
    for (int track : uniqueTracks) tracks.append(track);
    std::sort(tracks.begin(), tracks.end());
    return !tracks.isEmpty();
}

bool downstreamGroupsStayInsideTracks(const std::shared_ptr<TimelineItemModel> &timeline, const QVector<int> &allowedTracks,
                                      int fromFrame, QString &error)
{
    const QSet<int> allowed = trackSet(allowedTracks);
    QSet<int> checkedLeaves;
    for (int trackId : allowedTracks) {
        const std::unordered_set<int> items = timeline->getItemsInRange(trackId, fromFrame, -1, false);
        for (int itemId : items) {
            if ((!timeline->isClip(itemId) && !timeline->isComposition(itemId)) || !timeline->isInGroup(itemId)) continue;
            if (checkedLeaves.contains(itemId)) continue;
            const std::unordered_set<int> leaves = timeline->getGroupElements(itemId);
            for (int leaf : leaves) {
                checkedLeaves.insert(leaf);
                if (!timeline->isItem(leaf)) continue;
                const int leafTrack = timeline->getItemTrackId(leaf);
                if (leafTrack >= 0 && !allowed.contains(leafTrack)) {
                    error = QStringLiteral("Ripple would desynchronize grouped item %1 because its group also uses track %2 outside the affected track set.")
                                .arg(itemId).arg(leafTrack);
                    return false;
                }
            }
        }
    }
    return true;
}

QJsonObject applyLinked(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface) return err(QStringLiteral("VibeCut tool surface is unavailable."));
    const std::shared_ptr<TimelineItemModel> timeline = currentModel();
    if (!timeline) return err(QStringLiteral("No active timeline is open."));

    QJsonObject planInput = input;
    planInput.remove(QStringLiteral("mode"));
    const QJsonObject plan = surface->invoke(QStringLiteral("dead_air_cleanup_plan"), planInput);
    if (!plan.value(QStringLiteral("ok")).toBool(false)) return plan;
    if (plan.value(QStringLiteral("truncated")).toBool(false)) {
        return err(QStringLiteral("Dead-air candidate list reached max_ranges; linked cleanup refuses partial execution."));
    }

    struct Work {
        int trackId;
        int start;
        int end;
        int frames;
        QString key;
    };
    std::vector<Work> work;
    QSet<QString> seen;
    for (const QJsonValue &value : plan.value(QStringLiteral("candidates")).toArray()) {
        const QJsonObject candidate = value.toObject();
        const int trackId = candidate.value(QStringLiteral("track_id")).toInt(-1);
        const int start = candidate.value(QStringLiteral("timeline_start_frame")).toInt(-1);
        const int end = candidate.value(QStringLiteral("timeline_end_frame")).toInt(-1);
        if (trackId < 0 || start < 0 || end <= start) return err(QStringLiteral("Dead-air plan contains an invalid linked candidate."));
        const int anchor = timeline->getClipByPosition(trackId, start);
        if (anchor < 0 || !timeline->isClip(anchor)) {
            return err(QStringLiteral("Could not resolve linked candidate anchor at track %1 frame %2.").arg(trackId).arg(start));
        }
        const QString key = groupRangeKey(timeline, anchor, start, end);
        if (seen.contains(key)) continue;
        seen.insert(key);
        Work item;
        item.trackId = trackId;
        item.start = start;
        item.end = end;
        item.frames = end - start;
        item.key = key;
        work.push_back(item);
    }
    if (work.empty()) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("changed"), false},
                           {QStringLiteral("removed_ranges"), 0},
                           {QStringLiteral("note"), QStringLiteral("No qualifying linked dead-air ranges were found.")}};
    }
    std::sort(work.begin(), work.end(), [](const Work &a, const Work &b) {
        if (a.start != b.start) return a.start > b.start;
        return a.trackId > b.trackId;
    });

    for (const Work &candidate : work) {
        const int anchor = timeline->getClipByPosition(candidate.trackId, candidate.start);
        QVector<int> tracks;
        QString safetyError;
        if (anchor < 0 || !timeline->isClip(anchor) ||
            !groupTracksForRange(timeline, anchor, candidate.start, candidate.end, tracks, safetyError)) {
            return err(safetyError.isEmpty() ? QStringLiteral("Linked dead-air group validation failed.") : safetyError);
        }
        if (!downstreamGroupsStayInsideTracks(timeline, tracks, candidate.start, safetyError)) return err(safetyError);
    }

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    qint64 removedFrames = 0;
    int removedRanges = 0;

    for (const Work &candidate : work) {
        int anchor = timeline->getClipByPosition(candidate.trackId, candidate.start);
        if (anchor < 0 || !timeline->isClip(anchor)) {
            undo();
            return err(QStringLiteral("Linked cleanup lost its anchor at frame %1; all changes rolled back.").arg(candidate.start));
        }
        QVector<int> allowedTracks;
        QString safetyError;
        if (!groupTracksForRange(timeline, anchor, candidate.start, candidate.end, allowedTracks, safetyError)) {
            undo();
            return err(QStringLiteral("%1 All changes rolled back.").arg(safetyError));
        }
        if (!downstreamGroupsStayInsideTracks(timeline, allowedTracks, candidate.start, safetyError)) {
            undo();
            return err(QStringLiteral("%1 All changes rolled back.").arg(safetyError));
        }

        const int anchorStart = timeline->getClipPosition(anchor);
        const int anchorEnd = anchorStart + timeline->getClipPlaytime(anchor);
        if (candidate.end < anchorEnd && !TimelineFunctions::requestClipCut(timeline, anchor, candidate.end, undo, redo)) {
            undo();
            return err(QStringLiteral("Failed linked cut at dead-air end %1; all changes rolled back.").arg(candidate.end));
        }
        anchor = timeline->getClipByPosition(candidate.trackId, candidate.start);
        if (anchor < 0 || !timeline->isClip(anchor)) {
            undo();
            return err(QStringLiteral("Could not resolve linked middle after end cut; all changes rolled back."));
        }
        if (candidate.start > timeline->getClipPosition(anchor) &&
            !TimelineFunctions::requestClipCut(timeline, anchor, candidate.start, undo, redo)) {
            undo();
            return err(QStringLiteral("Failed linked cut at dead-air start %1; all changes rolled back.").arg(candidate.start));
        }

        const int middle = timeline->getClipByPosition(candidate.trackId, candidate.start);
        if (middle < 0 || !timeline->isClip(middle) || timeline->getClipPosition(middle) != candidate.start) {
            undo();
            return err(QStringLiteral("Could not resolve exact linked middle segment; all changes rolled back."));
        }
        const std::unordered_set<int> middleLeaves = timeline->getGroupElements(middle);
        if (middleLeaves.empty()) {
            undo();
            return err(QStringLiteral("Linked middle segment has no group leaves; all changes rolled back."));
        }
        QSet<int> middleTracks;
        for (int leaf : middleLeaves) {
            if (!timeline->isClip(leaf) || timeline->getClipPosition(leaf) != candidate.start || timeline->getClipPlaytime(leaf) != candidate.frames) {
                undo();
                return err(QStringLiteral("Linked middle group did not split into identical dead-air spans; all changes rolled back."));
            }
            middleTracks.insert(timeline->getClipTrackId(leaf));
        }
        if (middleTracks != trackSet(allowedTracks)) {
            undo();
            return err(QStringLiteral("Linked middle group track set changed unexpectedly; all changes rolled back."));
        }

        if (!timeline->requestItemDeletion(middle, undo, redo, false)) {
            undo();
            return err(QStringLiteral("Failed to delete linked dead-air group; all changes rolled back."));
        }
        for (int leaf : middleLeaves) {
            if (timeline->isItem(leaf)) {
                undo();
                return err(QStringLiteral("Linked dead-air leaf %1 is still present after deletion; all changes rolled back.").arg(leaf));
            }
        }
        if (!TimelineFunctions::removeSpace(timeline, QPoint(candidate.start, candidate.end), undo, redo, allowedTracks, false)) {
            undo();
            return err(QStringLiteral("Failed to ripple linked tracks after deleting [%1,%2); all changes rolled back.")
                           .arg(candidate.start).arg(candidate.end));
        }
        removedFrames += candidate.frames;
        ++removedRanges;
    }

    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: remove %1 linked dead-air range(s)").arg(removedRanges));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("changed"), true},
                       {QStringLiteral("mode"), QStringLiteral("ripple_linked_group")},
                       {QStringLiteral("removed_ranges"), removedRanges},
                       {QStringLiteral("removed_frames"), removedFrames},
                       {QStringLiteral("removed_seconds"), removedFrames / pCore->getCurrentFps()},
                       {QStringLiteral("verified"), true}};
}
} // namespace

bool registerVibeCutDeadLinkedTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("min_silence_ms"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 50}, {QStringLiteral("maximum"), 600000}}},
                                {QStringLiteral("keep_padding_ms"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 10000}}},
                                {QStringLiteral("max_ranges"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 1000}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("dead_air_cleanup_apply_linked");
    policy.risk = VibeCutToolRisk::MajorEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Apply evidence-backed dead-air cleanup across a linked clip group and ripple exactly the group's participating tracks together. Deduplicates split A/V candidates, uses Kdenlive group-aware cuts/deletion, refuses non-clip group leaves or downstream groups that extend outside the affected tracks, verifies identical middle spans, and rolls back the entire operation on any failure.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, [&surface](const QJsonObject &input) { return applyLinked(&surface, input); }, error);
}
