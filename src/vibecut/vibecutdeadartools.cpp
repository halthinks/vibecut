/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutdeadartools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecutmediaevidence.h"
#include "vibecuttoolsurface.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>

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

QJsonObject planDeadAir(const QJsonObject &input)
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
                       {QStringLiteral("candidates"), candidates},
                       {QStringLiteral("execution_ready"), false},
                       {QStringLiteral("note"), QStringLiteral("This is an evidence-backed review artifact only. It maps current silence evidence through each timeline clip's source in-point into exact timeline ranges. A separate transactional executor must resolve split/delete/ripple ordering and grouped/multitrack consequences before mutation.")}};
}
} // namespace

bool registerVibeCutDeadAirTools(VibeCutToolSurface &surface, QString *error)
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
    policy.name = QStringLiteral("dead_air_cleanup_plan");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Build a non-mutating dead-air cleanup proposal by intersecting current silence evidence with every active-timeline instance of one bin asset, translating source-frame silence through each clip's source in-point into exact timeline removal ranges with configurable minimum duration and retained padding.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, planDeadAir, error);
}
