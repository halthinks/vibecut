/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecutsubtitletools.h"

#include "bin/model/subtitlemodel.hpp"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>

#include <algorithm>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

TimelineWidget *currentTimeline()
{
    if (!pCore || !pCore->window()) {
        return nullptr;
    }
    return pCore->window()->getCurrentTimeline();
}

std::shared_ptr<TimelineItemModel> currentModel()
{
    TimelineWidget *timeline = currentTimeline();
    return timeline ? timeline->model() : nullptr;
}

struct SubtitleMatch {
    int id = -1;
    int layer = 0;
    int startFrame = 0;
    int endFrame = 0;
    QString text;
};

QJsonObject searchSubtitles(const QJsonObject &input)
{
    const QString query = input.value(QStringLiteral("query")).toString().trimmed();
    if (query.isEmpty()) {
        return err(QStringLiteral("query must not be empty"));
    }

    const bool caseSensitive = input.value(QStringLiteral("case_sensitive")).toBool(false);
    const int limit = qBound(1, input.value(QStringLiteral("limit")).toInt(25), 100);
    const Qt::CaseSensitivity sensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) {
        return err(QStringLiteral("No timeline is open."));
    }
    if (!model->hasSubtitleModel()) {
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("query"), query},
                           {QStringLiteral("match_count"), 0},
                           {QStringLiteral("returned_count"), 0},
                           {QStringLiteral("truncated"), false},
                           {QStringLiteral("matches"), QJsonArray{}}};
    }

    const std::shared_ptr<SubtitleModel> subtitles = model->getSubtitleModel();
    if (!subtitles) {
        return err(QStringLiteral("The timeline reports subtitles but its subtitle model is unavailable."));
    }

    QVector<SubtitleMatch> matches;
    const std::unordered_set<int> ids = subtitles->getAllSubIds();
    matches.reserve(static_cast<int>(ids.size()));
    for (int id : ids) {
        const QString text = subtitles->getText(id);
        if (!text.contains(query, sensitivity)) {
            continue;
        }
        SubtitleMatch match;
        match.id = id;
        match.layer = subtitles->getLayerForId(id);
        match.startFrame = model->getSubtitlePosition(id);
        match.endFrame = subtitles->getSubtitleEnd(id);
        match.text = text;
        matches.append(match);
    }

    std::sort(matches.begin(), matches.end(), [](const SubtitleMatch &a, const SubtitleMatch &b) {
        if (a.startFrame != b.startFrame) {
            return a.startFrame < b.startFrame;
        }
        return a.id < b.id;
    });

    const int totalMatches = matches.size();
    QJsonArray resultMatches;
    for (int i = 0; i < totalMatches && i < limit; ++i) {
        const SubtitleMatch &match = matches.at(i);
        resultMatches.append(QJsonObject{
            {QStringLiteral("id"), match.id},
            {QStringLiteral("layer"), match.layer},
            {QStringLiteral("start_frame"), match.startFrame},
            {QStringLiteral("end_frame"), match.endFrame},
            {QStringLiteral("duration_frames"), qMax(0, match.endFrame - match.startFrame)},
            {QStringLiteral("text"), match.text},
        });
    }

    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("query"), query},
        {QStringLiteral("case_sensitive"), caseSensitive},
        {QStringLiteral("match_count"), totalMatches},
        {QStringLiteral("returned_count"), resultMatches.size()},
        {QStringLiteral("truncated"), totalMatches > resultMatches.size()},
        {QStringLiteral("matches"), resultMatches},
    };
}

QJsonObject scopedSubtitleGeneration(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface) {
        return err(QStringLiteral("Subtitle tool surface is unavailable."));
    }

    QJsonObject normalized = input;
    const QString scope = normalized.value(QStringLiteral("scope")).toString(QStringLiteral("auto"));
    if (scope != QLatin1String("auto") && scope != QLatin1String("whole_project")) {
        return err(QStringLiteral("scope must be 'auto' or 'whole_project'"));
    }
    normalized.remove(QStringLiteral("scope"));

    // An explicit clip id is already an explicit scope; preserve the native
    // handler's validation and behavior.
    if (normalized.contains(QStringLiteral("clip_id"))) {
        return surface->invokeBase(QStringLiteral("generate_subtitles"), normalized);
    }
    if (scope == QLatin1String("whole_project")) {
        return surface->invokeBase(QStringLiteral("generate_subtitles"), normalized);
    }

    TimelineWidget *timeline = currentTimeline();
    const std::shared_ptr<TimelineItemModel> model = timeline ? timeline->model() : nullptr;
    if (!timeline || !model) {
        return err(QStringLiteral("No timeline is open."));
    }

    TimelineController *controller = timeline->controller();
    const int selected = controller ? controller->getMainSelectedClip() : -1;
    if (selected != -1 && model->isClip(selected)) {
        normalized.insert(QStringLiteral("clip_id"), selected);
        return surface->invokeBase(QStringLiteral("generate_subtitles"), normalized);
    }

    QList<int> candidates;
    for (int trackId : model->getAllTracksIds()) {
        for (int clipId : model->getItemsInRange(trackId, 0, -1, false)) {
            if (model->isClip(clipId)) {
                candidates.append(clipId);
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(), [model](int a, int b) {
        const int aPos = model->getClipPosition(a);
        const int bPos = model->getClipPosition(b);
        return aPos == bPos ? a < b : aPos < bPos;
    });

    if (candidates.size() == 1) {
        normalized.insert(QStringLiteral("clip_id"), candidates.first());
        return surface->invokeBase(QStringLiteral("generate_subtitles"), normalized);
    }
    if (candidates.isEmpty()) {
        return err(QStringLiteral("There are no timeline clips to transcribe."));
    }

    QJsonArray candidateData;
    for (int clipId : candidates) {
        candidateData.append(QJsonObject{{QStringLiteral("clip_id"), clipId},
                                         {QStringLiteral("name"), model->getClipName(clipId)},
                                         {QStringLiteral("start_frame"), model->getClipPosition(clipId)},
                                         {QStringLiteral("duration_frames"), model->getClipPlaytime(clipId)}});
    }
    return QJsonObject{{QStringLiteral("ok"), false},
                       {QStringLiteral("error"),
                        QStringLiteral("Subtitle scope is ambiguous: select a clip, pass clip_id, or explicitly set scope='whole_project'.")},
                       {QStringLiteral("candidates"), candidateData}};
}

QJsonObject subtitleSearchSchema()
{
    QJsonObject inputSchema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("query"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                          {QStringLiteral("description"), QStringLiteral("Text to find in the active timeline's subtitles.")}}},
             {QStringLiteral("case_sensitive"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                          {QStringLiteral("description"), QStringLiteral("Use case-sensitive matching. Defaults to false.")}}},
             {QStringLiteral("limit"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                          {QStringLiteral("minimum"), 1},
                          {QStringLiteral("maximum"), 100},
                          {QStringLiteral("description"), QStringLiteral("Maximum matches to return. Defaults to 25.")}}},
         }},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("query")}},
        {QStringLiteral("additionalProperties"), false},
    };

    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("subtitles_search")},
        {QStringLiteral("description"),
         QStringLiteral("Search existing subtitles on the active timeline and return matching text with subtitle ids, layers and frame ranges. "
                        "This is read-only: it never creates or edits a subtitle track.")},
        {QStringLiteral("input_schema"), inputSchema},
    };
}

QJsonObject subtitleGenerationSchema()
{
    QJsonObject inputSchema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("clip_id"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                          {QStringLiteral("description"), QStringLiteral("Transcribe exactly this timeline clip's span.")}}},
             {QStringLiteral("scope"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                          {QStringLiteral("enum"), QJsonArray{QStringLiteral("auto"), QStringLiteral("whole_project")}},
                          {QStringLiteral("description"),
                           QStringLiteral("Scope when clip_id is omitted. 'auto' (default) uses the selected clip, or the only timeline clip. "
                                          "If several clips exist with none selected it fails as ambiguous; use 'whole_project' only when the user "
                                          "explicitly wants the full timeline.")}}},
             {QStringLiteral("model"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                          {QStringLiteral("description"), QStringLiteral("Installed Whisper model to use; omit to choose the best installed default.")}}},
         }},
        {QStringLiteral("additionalProperties"), false},
    };

    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("generate_subtitles")},
        {QStringLiteral("description"),
         QStringLiteral("Transcribe audio with Whisper and add subtitles. Scope is conservative by default: use the selected clip or only clip; "
                        "never silently transcribe a multi-clip whole project. If scope is ambiguous, ask the user whether to choose a clip or the "
                        "whole project. Runs in the background after its audio-export preparation step.")},
        {QStringLiteral("input_schema"), inputSchema},
    };
}
} // namespace

bool registerVibeCutSubtitleTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutToolPolicy searchPolicy;
    searchPolicy.name = QStringLiteral("subtitles_search");
    searchPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(subtitleSearchSchema(), searchPolicy, searchSubtitles, error)) {
        return false;
    }

    VibeCutToolPolicy generationPolicy;
    generationPolicy.name = QStringLiteral("generate_subtitles");
    generationPolicy.risk = VibeCutToolRisk::MajorEdit;
    generationPolicy.reversible = true;
    generationPolicy.mutatesProject = true;
    generationPolicy.asynchronous = true;

    return surface.overrideBaseTool(subtitleGenerationSchema(), generationPolicy,
                                    [&surface](const QJsonObject &input) { return scopedSubtitleGeneration(&surface, input); }, error);
}
