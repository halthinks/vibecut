/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutdeadartools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelinefunctions.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecutmediaevidence.h"
#include "vibecuttoolsurface.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QtMath>

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

QString statFingerprint(const QFileInfo &info)
{
    const QByteArray payload = info.canonicalFilePath().toUtf8() + '\n' +
                               QByteArray::number(info.size()) + '\n' +
                               QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

struct SilenceRange {
    int start = -1;
    int end = -1;
};

QJsonObject buildDeadAirPlan(const QJsonObject &input)
{
    if (!pCore) return err(QStringLiteral("Kdenlive core is unavailable."));
    const std::shared_ptr<TimelineItemModel> timeline = currentModel();
    if (!timeline) return err(QStringLiteral("No active timeline is open."));

    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty."));
    const int minSilenceMs = qBound(50, input.value(QStringLiteral("min_silence_ms")).toInt(600), 600000);
    const int keepPaddingMs = qBound(0, input.value(QStringLiteral("keep_padding_ms")).toInt(120), 10000);
    const int maxRanges = qBound(1, input.value(QStringLiteral("max_ranges")).toInt(200), 1000);

    const std::shared_ptr<ProjectItemModel> bin = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> sourceClip = bin ? bin->getClipByBinID(binId) : nullptr;
    if (!sourceClip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    if (!sourceClip->hasUrl() || !sourceClip->hasAudio()) {
        return err(QStringLiteral("Dead-air planning requires a file-backed source with audio."));
    }
    const QFileInfo sourceInfo(sourceClip->url());
    if (!sourceInfo.exists() || !sourceInfo.isFile()) return err(QStringLiteral("Source file is missing or invalid."));
    const QString fingerprint = statFingerprint(sourceInfo);
    const QString sourceId = QStringLiteral("bin:%1").arg(binId);
    const double fps = pCore->getCurrentFps();
    if (fps <= 0.0) return err(QStringLiteral("Current project frame rate is invalid."));
    const int minFrames = qMax(1, static_cast<int>(qCeil((minSilenceMs / 1000.0) * fps)));
    const int paddingFrames = qMax(0, static_cast<int>(qRound((keepPaddingMs / 1000.0) * fps)));

    QString evidenceError;
    const QJsonArray evidence = VibeCutMediaEvidence::loadCurrent(&evidenceError);
    if (!evidenceError.isEmpty()) return err(evidenceError);
    std::vector<SilenceRange> silence;
    for (const QJsonValue &value : evidence) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("source_id")).toString() != sourceId) continue;
        if (object.value(QStringLiteral("source_fingerprint")).toString() != fingerprint) continue;
        if (object.value(QStringLiteral("extractor_id")).toString() != QLatin1String("silence_detect")) continue;
        if (object.value(QStringLiteral("kind")).toString() != QLatin1String("silence")) continue;
        const int start = object.value(QStringLiteral("start_frame")).toInt(-1);
        const int end = object.value(QStringLiteral("end_frame")).toInt(-1);
        if (start >= 0 && end >= start) silence.push_back({start, end});
    }
    if (silence.empty()) {
        return err(QStringLiteral("No current silence evidence exists for bin '%1'. Run media_silence_refresh first or refresh stale media analysis.").arg(binId));
    }
    std::sort(silence.begin(), silence.end(), [](const SilenceRange &a, const SilenceRange &b) { return a.start < b.start; });

    QJsonArray candidates;
    qint64 totalFrames = 0;
    int timelineInstances = 0;
    for (int trackId : timeline->getAllTracksIds()) {
        for (int clipId : timeline->getItemsInRange(trackId, 0, -1, false)) {
            if (!timeline->isClip(clipId) || timeline->getClipBinId(clipId) != binId) continue;
            ++timelineInstances;
            const int sourceIn = timeline->getClipIn(clipId);
            const int playtime = timeline->getClipPlaytime(clipId);
            const int sourceOut = sourceIn + playtime;
            const int timelinePos = timeline->getClipPosition(clipId);

            for (const SilenceRange &range : silence) {
                int overlapStart = qMax(sourceIn, range.start);
                int overlapEnd = qMin(sourceOut, range.end);
                if (overlapEnd <= overlapStart) continue;

                overlapStart += paddingFrames;
                overlapEnd -= paddingFrames;
                if (overlapEnd <= overlapStart || overlapEnd - overlapStart < minFrames) continue;

                const int timelineStart = timelinePos + (overlapStart - sourceIn);
                const int timelineEnd = timelinePos + (overlapEnd - sourceIn);
                const int duration = timelineEnd - timelineStart;
                if (duration < minFrames) continue;

                candidates.append(QJsonObject{{QStringLiteral("clip_id"), clipId},
                                              {QStringLiteral("track_id"), trackId},
                                              {QStringLiteral("bin_id"), binId},
                                              {QStringLiteral("grouped"), timeline->isInGroup(clipId)},
                                              {QStringLiteral("source_start_frame"), overlapStart},
                                              {QStringLiteral("source_end_frame"), overlapEnd},
                                              {QStringLiteral("timeline_start_frame"), timelineStart},
                                              {QStringLiteral("timeline_end_frame"), timelineEnd},
                                              {QStringLiteral("remove_frames"), duration},
                                              {QStringLiteral("remove_seconds"), duration / fps}});
                totalFrames += duration;
                if (candidates.size() >= maxRanges) break;
            }
            if (candidates.size() >= maxRanges) break;
        }
        if (candidates.size() >= maxRanges) break;
    }

    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("bin_id"), binId},
                       {QStringLiteral("source_id"), sourceId},
                       {QStringLiteral("source_fingerprint"), fingerprint},
                       {QStringLiteral("timeline_instance_count"), timelineInstances},
                       {QStringLiteral("silence_evidence_count"), static_cast<int>(silence.size())},
                       {QStringLiteral("candidate_count"), candidates.size()},
                       {QStringLiteral("total_remove_frames"), totalFrames},
                       {QStringLiteral("total_remove_seconds"), totalFrames / fps},
                       {QStringLiteral("min_silence_ms"), minSilenceMs},
                       {QStringLiteral("keep_padding_ms"), keepPaddingMs},
                       {QStringLiteral("truncated"), candidates.size() >= maxRanges},
                       {QStringLiteral("candidates"), candidates}};
}

QJsonObject planDeadAir(const QJsonObject &input)
{
    QJsonObject result = buildDeadAirPlan(input);
    if (!result.value(QStringLiteral("ok")).toBool(false)) return result;
    result.insert(QStringLiteral("execution_ready"), false);
    result.insert(QStringLiteral("note"), QStringLiteral("Review artifact only. Call dead_air_cleanup_apply with an explicit mode after reviewing these evidence-backed timeline ranges."));
    return result;
}

QJsonObject applyDeadAir(const QJsonObject &input)
{
    const QString mode = input.value(QStringLiteral("mode")).toString().trimmed().toLower();
    if (mode != QLatin1String("lift") && mode != QLatin1String("ripple_single_track")) {
        return err(QStringLiteral("mode must be 'lift' or 'ripple_single_track'."));
    }

    QJsonObject plan = buildDeadAirPlan(input);
    if (!plan.value(QStringLiteral("ok")).toBool(false)) return plan;
    if (plan.value(QStringLiteral("truncated")).toBool(false)) {
        return err(QStringLiteral("Dead-air candidate list reached max_ranges; refuse partial cleanup. Increase max_ranges or narrow the scope."));
    }
    const QJsonArray candidates = plan.value(QStringLiteral("candidates")).toArray();
    if (candidates.isEmpty()) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("changed"), false},
                           {QStringLiteral("mode"), mode}, {QStringLiteral("removed_ranges"), 0},
                           {QStringLiteral("note"), QStringLiteral("No qualifying dead-air ranges were found.")}};
    }

    const std::shared_ptr<TimelineItemModel> timeline = currentModel();
    if (!timeline) return err(QStringLiteral("No active timeline is open."));

    struct Candidate {
        int clipId;
        int trackId;
        int start;
        int end;
        int frames;
    };
    std::vector<Candidate> work;
    work.reserve(candidates.size());
    for (const QJsonValue &value : candidates) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("grouped")).toBool(false)) {
            return err(QStringLiteral("Dead-air cleanup currently refuses grouped source clips; use the review plan or ungroup/implement a linked multitrack cleanup path first."));
        }
        const Candidate candidate{object.value(QStringLiteral("clip_id")).toInt(-1),
                                  object.value(QStringLiteral("track_id")).toInt(-1),
                                  object.value(QStringLiteral("timeline_start_frame")).toInt(-1),
                                  object.value(QStringLiteral("timeline_end_frame")).toInt(-1),
                                  object.value(QStringLiteral("remove_frames")).toInt(0)};
        if (candidate.clipId < 0 || candidate.trackId < 0 || candidate.start < 0 || candidate.end <= candidate.start) {
            return err(QStringLiteral("Dead-air plan contains an invalid candidate range."));
        }
        work.push_back(candidate);
    }

    if (mode == QLatin1String("ripple_single_track")) {
        for (const Candidate &candidate : work) {
            for (int itemId : timeline->getItemsInRange(candidate.trackId, candidate.start, -1, false)) {
                if ((timeline->isClip(itemId) || timeline->isComposition(itemId)) && timeline->isInGroup(itemId)) {
                    return err(QStringLiteral("Single-track ripple is unsafe because grouped item %1 exists at/after frame %2 on track %3. Use lift mode or a future linked multitrack ripple executor.")
                                   .arg(itemId).arg(candidate.start).arg(candidate.trackId));
                }
            }
        }
    }

    std::sort(work.begin(), work.end(), [](const Candidate &a, const Candidate &b) {
        if (a.start != b.start) return a.start > b.start;
        return a.trackId > b.trackId;
    });

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QList<int> deletedIds;
    qint64 removedFrames = 0;

    for (const Candidate &candidate : work) {
        int liveClip = timeline->getClipByPosition(candidate.trackId, candidate.start);
        if (liveClip < 0 || !timeline->isClip(liveClip)) {
            undo();
            return err(QStringLiteral("Dead-air execution could not resolve a live clip at track %1 frame %2; all changes rolled back.").arg(candidate.trackId).arg(candidate.start));
        }
        if (timeline->isInGroup(liveClip)) {
            undo();
            return err(QStringLiteral("Clip %1 became grouped before execution; all changes rolled back.").arg(liveClip));
        }
        const int liveStart = timeline->getClipPosition(liveClip);
        const int liveEnd = liveStart + timeline->getClipPlaytime(liveClip);
        if (candidate.start < liveStart || candidate.end > liveEnd) {
            undo();
            return err(QStringLiteral("Dead-air range [%1,%2) no longer fits live clip %3 [%4,%5); all changes rolled back.")
                           .arg(candidate.start).arg(candidate.end).arg(liveClip).arg(liveStart).arg(liveEnd));
        }

        if (candidate.end < liveEnd && !TimelineFunctions::requestClipCut(timeline, liveClip, candidate.end, undo, redo)) {
            undo();
            return err(QStringLiteral("Failed to cut dead-air end at frame %1; all changes rolled back.").arg(candidate.end));
        }

        liveClip = timeline->getClipByPosition(candidate.trackId, candidate.start);
        if (liveClip < 0 || !timeline->isClip(liveClip)) {
            undo();
            return err(QStringLiteral("Could not resolve dead-air segment after end cut; all changes rolled back."));
        }
        const int segmentStart = timeline->getClipPosition(liveClip);
        if (candidate.start > segmentStart && !TimelineFunctions::requestClipCut(timeline, liveClip, candidate.start, undo, redo)) {
            undo();
            return err(QStringLiteral("Failed to cut dead-air start at frame %1; all changes rolled back.").arg(candidate.start));
        }

        const int middleId = timeline->getClipByPosition(candidate.trackId, candidate.start);
        if (middleId < 0 || !timeline->isClip(middleId) || timeline->getClipPosition(middleId) != candidate.start) {
            undo();
            return err(QStringLiteral("Could not identify exact middle dead-air segment at frame %1; all changes rolled back.").arg(candidate.start));
        }
        const int middleDuration = timeline->getClipPlaytime(middleId);
        if (middleDuration != candidate.end - candidate.start) {
            undo();
            return err(QStringLiteral("Dead-air middle segment duration mismatch (%1 vs expected %2); all changes rolled back.")
                           .arg(middleDuration).arg(candidate.end - candidate.start));
        }
        if (!timeline->requestItemDeletion(middleId, undo, redo, false)) {
            undo();
            return err(QStringLiteral("Failed to delete dead-air middle segment %1; all changes rolled back.").arg(middleId));
        }
        if (timeline->isItem(middleId)) {
            undo();
            return err(QStringLiteral("Deleted dead-air segment %1 is still present; all changes rolled back.").arg(middleId));
        }
        deletedIds.append(middleId);

        if (mode == QLatin1String("ripple_single_track")) {
            if (!TimelineFunctions::removeSpace(timeline, QPoint(candidate.start, candidate.end), undo, redo,
                                                QVector<int>{candidate.trackId}, false)) {
                undo();
                return err(QStringLiteral("Failed to close dead-air blank [%1,%2) on track %3; all changes rolled back.")
                               .arg(candidate.start).arg(candidate.end).arg(candidate.trackId));
            }
        }
        removedFrames += candidate.frames;
    }

    for (int id : deletedIds) {
        if (timeline->isItem(id)) {
            undo();
            return err(QStringLiteral("Final dead-air verification found deleted item %1 still present; operation rolled back.").arg(id));
        }
    }

    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: remove %1 dead-air range(s)").arg(work.size()));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("changed"), true},
                       {QStringLiteral("mode"), mode}, {QStringLiteral("removed_ranges"), static_cast<int>(work.size())},
                       {QStringLiteral("removed_frames"), removedFrames},
                       {QStringLiteral("removed_seconds"), removedFrames / pCore->getCurrentFps()},
                       {QStringLiteral("verified"), true}};
}

QJsonObject commonProperties()
{
    return QJsonObject{{QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                       {QStringLiteral("min_silence_ms"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 50}, {QStringLiteral("maximum"), 600000}}},
                       {QStringLiteral("keep_padding_ms"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 10000}}},
                       {QStringLiteral("max_ranges"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 1000}}}};
}
} // namespace

bool registerVibeCutDeadAirTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject planInput{{QStringLiteral("type"), QStringLiteral("object")},
                                {QStringLiteral("properties"), commonProperties()},
                                {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                                {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy planPolicy;
    planPolicy.name = QStringLiteral("dead_air_cleanup_plan");
    planPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), planPolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Build a non-mutating dead-air cleanup proposal by intersecting current silence evidence with every active-timeline instance of one bin asset, translating source-frame silence through each clip's source in-point into exact timeline removal ranges with configurable minimum duration and retained padding.")},
                                          {QStringLiteral("input_schema"), planInput}},
                              planPolicy, planDeadAir, error)) return false;

    QJsonObject applyProps = commonProperties();
    applyProps.insert(QStringLiteral("mode"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                          {QStringLiteral("enum"), QJsonArray{QStringLiteral("lift"), QStringLiteral("ripple_single_track")}}});
    const QJsonObject applyInput{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), applyProps},
                                 {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id"), QStringLiteral("mode")}},
                                 {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy applyPolicy;
    applyPolicy.name = QStringLiteral("dead_air_cleanup_apply");
    applyPolicy.risk = VibeCutToolRisk::MajorEdit;
    applyPolicy.reversible = true;
    applyPolicy.mutatesProject = true;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), applyPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Apply current evidence-backed dead-air ranges as one rollback-safe Kdenlive undo transaction. 'lift' leaves gaps. 'ripple_single_track' closes each gap only when downstream grouped material cannot be desynchronized. Grouped source clips are refused pending a linked multitrack executor.")},
                                            {QStringLiteral("input_schema"), applyInput}},
                                applyPolicy, applyDeadAir, error);
}
