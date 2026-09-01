/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutrepeatedtaketools.h"

#include "bin/model/subtitlemodel.hpp"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <numeric>
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

QString normalizeText(QString text)
{
    text = text.toLower();
    text.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" "));
    return text.simplified();
}

QStringList tokens(const QString &normalized)
{
    return normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

double tokenJaccard(const QStringList &a, const QStringList &b)
{
    QSet<QString> sa;
    QSet<QString> sb;
    for (const QString &word : a) sa.insert(word);
    for (const QString &word : b) sb.insert(word);
    if (sa.isEmpty() || sb.isEmpty()) return 0.0;
    int intersection = 0;
    for (const QString &word : sa) if (sb.contains(word)) ++intersection;
    const int unionCount = sa.size() + sb.size() - intersection;
    return unionCount > 0 ? static_cast<double>(intersection) / unionCount : 0.0;
}

double orderedOverlap(const QStringList &a, const QStringList &b)
{
    if (a.isEmpty() || b.isEmpty()) return 0.0;
    const int n = a.size();
    const int m = b.size();
    QVector<int> previous(m + 1, 0);
    QVector<int> current(m + 1, 0);
    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            current[j] = a.at(i - 1) == b.at(j - 1) ? previous[j - 1] + 1 : qMax(previous[j], current[j - 1]);
        }
        previous = current;
        std::fill(current.begin(), current.end(), 0);
    }
    return static_cast<double>(previous[m]) / qMax(n, m);
}

struct Segment {
    int subtitleId = -1;
    int start = -1;
    int end = -1;
    QString text;
    QString normalized;
    QStringList words;
};

struct Dsu {
    QVector<int> parent;
    explicit Dsu(int n) : parent(n) { std::iota(parent.begin(), parent.end(), 0); }
    int find(int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); }
    void unite(int a, int b) { a = find(a); b = find(b); if (a != b) parent[b] = a; }
};

QJsonArray clipsAt(const std::shared_ptr<TimelineItemModel> &timeline, int frame)
{
    QJsonArray result;
    for (int trackId : timeline->getAllTracksIds()) {
        const int clipId = timeline->getClipByPosition(trackId, frame);
        if (clipId < 0 || !timeline->isClip(clipId)) continue;
        result.append(QJsonObject{{QStringLiteral("clip_id"), clipId},
                                  {QStringLiteral("track_id"), trackId},
                                  {QStringLiteral("bin_id"), timeline->getClipBinId(clipId)},
                                  {QStringLiteral("clip_name"), timeline->getClipName(clipId)},
                                  {QStringLiteral("source_in_frame"), timeline->getClipIn(clipId)},
                                  {QStringLiteral("timeline_position_frame"), timeline->getClipPosition(clipId)},
                                  {QStringLiteral("duration_frames"), timeline->getClipPlaytime(clipId)}});
    }
    return result;
}

QJsonObject findRepeated(const QJsonObject &input)
{
    const std::shared_ptr<TimelineItemModel> timeline = currentModel();
    if (!timeline || !timeline->hasSubtitleModel()) return err(QStringLiteral("The active timeline has no subtitle/transcript model."));
    const std::shared_ptr<SubtitleModel> subtitles = timeline->getSubtitleModel();
    if (!subtitles) return err(QStringLiteral("Subtitle model is unavailable."));
    const double fps = pCore ? pCore->getCurrentFps() : 0.0;
    if (fps <= 0.0) return err(QStringLiteral("Current project frame rate is invalid."));

    const int minWords = qBound(3, input.value(QStringLiteral("min_words")).toInt(6), 100);
    const double threshold = qBound(0.50, input.value(QStringLiteral("similarity_threshold")).toDouble(0.78), 1.0);
    const int maxSegments = qBound(2, input.value(QStringLiteral("max_segments")).toInt(300), 1000);
    const int maxGroups = qBound(1, input.value(QStringLiteral("max_groups")).toInt(100), 500);

    std::vector<Segment> segments;
    for (int subtitleId : subtitles->getAllSubIds()) {
        Segment segment;
        segment.subtitleId = subtitleId;
        segment.text = subtitles->getText(subtitleId).trimmed();
        segment.normalized = normalizeText(segment.text);
        segment.words = tokens(segment.normalized);
        if (segment.words.size() < minWords) continue;
        segment.start = subtitles->getSubtitlePosition(subtitleId).frames(fps);
        segment.end = subtitles->getSubtitleEnd(subtitleId);
        if (segment.start < 0 || segment.end <= segment.start) continue;
        segments.push_back(segment);
        if (static_cast<int>(segments.size()) >= maxSegments) break;
    }
    if (segments.size() < 2) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("group_count"), 0},
                           {QStringLiteral("groups"), QJsonArray{}},
                           {QStringLiteral("segments_considered"), static_cast<int>(segments.size())}};
    }

    Dsu groups(static_cast<int>(segments.size()));
    QVector<double> best(static_cast<int>(segments.size()), 0.0);
    int pairCount = 0;
    for (int i = 0; i < static_cast<int>(segments.size()); ++i) {
        for (int j = i + 1; j < static_cast<int>(segments.size()); ++j) {
            const double jaccard = tokenJaccard(segments[i].words, segments[j].words);
            if (jaccard < threshold * 0.70) continue;
            const double ordered = orderedOverlap(segments[i].words, segments[j].words);
            const double similarity = 0.45 * jaccard + 0.55 * ordered;
            if (similarity < threshold) continue;
            groups.unite(i, j);
            best[i] = qMax(best[i], similarity);
            best[j] = qMax(best[j], similarity);
            ++pairCount;
        }
    }

    QHash<int, QVector<int>> members;
    for (int i = 0; i < static_cast<int>(segments.size()); ++i) members[groups.find(i)].append(i);

    struct CandidateGroup { QVector<int> members; double score = 0.0; };
    std::vector<CandidateGroup> repeated;
    for (auto it = members.constBegin(); it != members.constEnd(); ++it) {
        if (it.value().size() < 2) continue;
        CandidateGroup group;
        group.members = it.value();
        for (int index : group.members) group.score = qMax(group.score, best[index]);
        repeated.push_back(group);
    }
    std::sort(repeated.begin(), repeated.end(), [&segments](const CandidateGroup &a, const CandidateGroup &b) {
        if (a.score != b.score) return a.score > b.score;
        return segments[a.members.first()].start < segments[b.members.first()].start;
    });

    QJsonArray outputGroups;
    int groupIndex = 0;
    for (const CandidateGroup &group : repeated) {
        if (groupIndex >= maxGroups) break;
        QJsonArray items;
        QVector<int> ordered = group.members;
        std::sort(ordered.begin(), ordered.end(), [&segments](int a, int b) { return segments[a].start < segments[b].start; });
        for (int index : ordered) {
            const Segment &segment = segments[index];
            const int midpoint = segment.start + (segment.end - segment.start) / 2;
            items.append(QJsonObject{{QStringLiteral("subtitle_id"), segment.subtitleId},
                                     {QStringLiteral("start_frame"), segment.start},
                                     {QStringLiteral("end_frame"), segment.end},
                                     {QStringLiteral("duration_frames"), segment.end - segment.start},
                                     {QStringLiteral("text"), segment.text},
                                     {QStringLiteral("normalized_text"), segment.normalized},
                                     {QStringLiteral("best_similarity"), best[index]},
                                     {QStringLiteral("timeline_clips"), clipsAt(timeline, midpoint)}});
        }
        outputGroups.append(QJsonObject{{QStringLiteral("group_index"), groupIndex++},
                                        {QStringLiteral("take_count"), items.size()},
                                        {QStringLiteral("similarity"), group.score},
                                        {QStringLiteral("takes"), items}});
    }

    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("segments_considered"), static_cast<int>(segments.size())},
                       {QStringLiteral("matching_pair_count"), pairCount},
                       {QStringLiteral("group_count"), outputGroups.size()},
                       {QStringLiteral("min_words"), minWords},
                       {QStringLiteral("similarity_threshold"), threshold},
                       {QStringLiteral("groups"), outputGroups},
                       {QStringLiteral("note"), QStringLiteral("Candidate grouping is deterministic transcript similarity only. No take is automatically preferred or deleted; use audiovisual quality evidence and editorial context before selecting a winner.")}};
}

QJsonObject reviewRepeated(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface) return err(QStringLiteral("VibeCut tool surface is unavailable."));
    const std::shared_ptr<TimelineItemModel> timeline = currentModel();
    if (!timeline) return err(QStringLiteral("No active timeline is open."));

    QJsonObject candidates = findRepeated(input);
    if (!candidates.value(QStringLiteral("ok")).toBool(false)) return candidates;

    QHash<QString, QJsonObject> qualityCache;
    QJsonArray enrichedGroups;
    for (const QJsonValue &groupValue : candidates.value(QStringLiteral("groups")).toArray()) {
        QJsonObject group = groupValue.toObject();
        QJsonArray enrichedTakes;
        for (const QJsonValue &takeValue : group.value(QStringLiteral("takes")).toArray()) {
            QJsonObject take = takeValue.toObject();
            const int takeStart = take.value(QStringLiteral("start_frame")).toInt(-1);
            const int takeEnd = take.value(QStringLiteral("end_frame")).toInt(-1);
            QJsonArray clipContexts;
            for (const QJsonValue &clipValue : take.value(QStringLiteral("timeline_clips")).toArray()) {
                QJsonObject clipObject = clipValue.toObject();
                const int clipId = clipObject.value(QStringLiteral("clip_id")).toInt(-1);
                if (clipId < 0 || !timeline->isClip(clipId)) continue;
                const QString binId = timeline->getClipBinId(clipId);
                const int clipPos = timeline->getClipPosition(clipId);
                const int clipDuration = timeline->getClipPlaytime(clipId);
                const int sourceIn = timeline->getClipIn(clipId);
                const int localStart = qBound(0, takeStart - clipPos, clipDuration);
                const int localEnd = qBound(0, takeEnd - clipPos, clipDuration);
                if (localEnd <= localStart) continue;
                const int sourceStart = sourceIn + localStart;
                const int sourceEnd = sourceIn + localEnd;
                const QString cacheKey = QStringLiteral("%1:%2:%3").arg(binId).arg(sourceStart).arg(sourceEnd);
                QJsonObject quality;
                if (qualityCache.contains(cacheKey)) {
                    quality = qualityCache.value(cacheKey);
                } else {
                    quality = surface->invoke(QStringLiteral("take_quality_context"),
                                              QJsonObject{{QStringLiteral("bin_id"), binId},
                                                          {QStringLiteral("start_frame"), sourceStart},
                                                          {QStringLiteral("end_frame"), sourceEnd}});
                    qualityCache.insert(cacheKey, quality);
                }
                clipObject.insert(QStringLiteral("source_start_frame"), sourceStart);
                clipObject.insert(QStringLiteral("source_end_frame"), sourceEnd);
                clipObject.insert(QStringLiteral("quality_context"), quality);
                clipContexts.append(clipObject);
            }
            take.insert(QStringLiteral("clip_quality_contexts"), clipContexts);
            enrichedTakes.append(take);
        }
        group.insert(QStringLiteral("takes"), enrichedTakes);
        enrichedGroups.append(group);
    }
    candidates.insert(QStringLiteral("groups"), enrichedGroups);
    candidates.insert(QStringLiteral("review_ready"), true);
    candidates.insert(QStringLiteral("note"), QStringLiteral("Repeated-take review combines transcript similarity with measured source-range evidence. It deliberately does not rank a winner automatically; missing/stale evidence remains visible in each quality context."));
    return candidates;
}

QJsonObject inputSchema()
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), QJsonObject{
                           {QStringLiteral("min_words"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 3}, {QStringLiteral("maximum"), 100}}},
                           {QStringLiteral("similarity_threshold"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.50}, {QStringLiteral("maximum"), 1.0}}},
                           {QStringLiteral("max_segments"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 2}, {QStringLiteral("maximum"), 1000}}},
                           {QStringLiteral("max_groups"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 500}}}}},
                       {QStringLiteral("additionalProperties"), false}};
}
} // namespace

bool registerVibeCutRepeatedTakeTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input = inputSchema();
    VibeCutToolPolicy candidatePolicy;
    candidatePolicy.name = QStringLiteral("repeated_take_candidates");
    candidatePolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), candidatePolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Find bounded repeated-take candidates from the active subtitle/transcript track using deterministic normalized-word Jaccard plus ordered-sequence overlap. Returns groups with exact subtitle/timeline ranges and underlying clip/bin identities, but deliberately does not choose or delete a take.")},
                                          {QStringLiteral("input_schema"), input}},
                              candidatePolicy, findRepeated, error)) return false;

    VibeCutToolPolicy reviewPolicy;
    reviewPolicy.name = QStringLiteral("repeated_take_review");
    reviewPolicy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), reviewPolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Build a repeated-take review artifact by combining deterministic transcript-similarity groups with exact source-frame take_quality_context evidence for every underlying clip occurrence. Missing/stale evidence remains visible and no automatic winner is selected.")},
                                            {QStringLiteral("input_schema"), input}},
                                reviewPolicy, [&surface](const QJsonObject &input) { return reviewRepeated(&surface, input); }, error);
}
