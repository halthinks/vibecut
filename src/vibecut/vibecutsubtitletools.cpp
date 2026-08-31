/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecutsubtitletools.h"

#include "bin/model/subtitlemodel.hpp"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>

#include <algorithm>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

std::shared_ptr<TimelineItemModel> currentModel()
{
    if (!pCore || !pCore->window()) {
        return nullptr;
    }
    TimelineWidget *timeline = pCore->window()->getCurrentTimeline();
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
} // namespace

bool registerVibeCutSubtitleTools(VibeCutToolSurface &surface, QString *error)
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

    const QJsonObject schema{
        {QStringLiteral("name"), QStringLiteral("subtitles_search")},
        {QStringLiteral("description"),
         QStringLiteral("Search existing subtitles on the active timeline and return matching text with subtitle ids, layers and frame ranges. "
                        "This is read-only: it never creates or edits a subtitle track.")},
        {QStringLiteral("input_schema"), inputSchema},
    };

    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("subtitles_search");
    policy.risk = VibeCutToolRisk::ReadOnly;

    return surface.registerTool(schema, policy, searchSubtitles, error);
}
