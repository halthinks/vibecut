/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecutmarkertools.h"

#include "bin/model/markerlistmodel.hpp"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
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

double currentFps()
{
    return pCore ? pCore->getCurrentFps() : 0.0;
}

QJsonObject listGuides(const QJsonObject &)
{
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    const std::shared_ptr<MarkerListModel> guides = model->getGuideModel();
    if (!guides) return err(QStringLiteral("The active timeline has no guide model."));
    const double fps = currentFps();
    if (fps <= 0.0) return err(QStringLiteral("Project frame rate is unavailable."));

    QJsonArray result;
    for (const CommentedTime &guide : guides->getAllMarkers()) {
        result.append(QJsonObject{{QStringLiteral("frame"), guide.time().frames(fps)},
                                  {QStringLiteral("duration_frames"), guide.duration().frames(fps)},
                                  {QStringLiteral("comment"), guide.comment()},
                                  {QStringLiteral("type"), guide.markerType()},
                                  {QStringLiteral("range"), guide.hasRange()}});
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("guides"), result}};
}

QJsonObject addGuide(const QJsonObject &input, bool range)
{
    const int frame = input.value(QStringLiteral("frame")).toInt(-1);
    const QString comment = input.value(QStringLiteral("comment")).toString().trimmed();
    const int type = input.value(QStringLiteral("type")).toInt(-1);
    const int durationFrames = range ? input.value(QStringLiteral("duration_frames")).toInt(-1) : 0;
    if (frame < 0) return err(QStringLiteral("frame must be >= 0"));
    if (comment.isEmpty()) return err(QStringLiteral("comment must not be empty"));
    if (range && durationFrames <= 0) return err(QStringLiteral("duration_frames must be > 0"));

    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    const std::shared_ptr<MarkerListModel> guides = model->getGuideModel();
    if (!guides) return err(QStringLiteral("The active timeline has no guide model."));
    const double fps = currentFps();
    if (fps <= 0.0) return err(QStringLiteral("Project frame rate is unavailable."));
    const GenTime position(frame, fps);
    const bool changed = range ? guides->addRangeMarker(position, GenTime(durationFrames, fps), comment, type)
                               : guides->addMarker(position, comment, type);
    if (!changed) return err(QStringLiteral("Kdenlive rejected the guide marker change at frame %1.").arg(frame));

    bool found = false;
    const CommentedTime verified = guides->getMarker(position, &found);
    if (!found || verified.comment() != comment || (range && verified.duration().frames(fps) != durationFrames)) {
        return err(QStringLiteral("Guide mutation returned success but live guide state did not match the requested marker."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("frame"), frame},
                       {QStringLiteral("duration_frames"), verified.duration().frames(fps)},
                       {QStringLiteral("comment"), verified.comment()}, {QStringLiteral("type"), verified.markerType()},
                       {QStringLiteral("range"), verified.hasRange()}, {QStringLiteral("verified"), true}};
}

QJsonObject removeGuide(const QJsonObject &input)
{
    const int frame = input.value(QStringLiteral("frame")).toInt(-1);
    if (frame < 0) return err(QStringLiteral("frame must be >= 0"));
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    const std::shared_ptr<MarkerListModel> guides = model->getGuideModel();
    if (!guides) return err(QStringLiteral("The active timeline has no guide model."));
    const double fps = currentFps();
    if (fps <= 0.0) return err(QStringLiteral("Project frame rate is unavailable."));
    const GenTime position(frame, fps);
    if (!guides->hasMarker(frame)) return err(QStringLiteral("There is no guide at frame %1.").arg(frame));
    if (!guides->removeMarker(position)) return err(QStringLiteral("Kdenlive rejected removing the guide at frame %1.").arg(frame));
    if (guides->hasMarker(frame)) return err(QStringLiteral("Guide removal returned success but a marker is still present at frame %1.").arg(frame));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("frame"), frame}, {QStringLiteral("verified"), true}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required}, {QStringLiteral("additionalProperties"), false}};
}

bool addTool(VibeCutToolSurface &surface, const QString &name, const QString &description, const QJsonObject &inputSchema,
             VibeCutToolRisk risk, bool mutates, const VibeCutToolSurface::Handler &handler, QString *error)
{
    VibeCutToolPolicy policy;
    policy.name = name;
    policy.risk = risk;
    policy.reversible = mutates;
    policy.mutatesProject = mutates;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("description"), description},
                                            {QStringLiteral("input_schema"), inputSchema}},
                                policy, handler, error);
}
} // namespace

bool registerVibeCutMarkerTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    if (!addTool(surface, QStringLiteral("guides_list"),
                 QStringLiteral("List timeline guides/markers with frame, duration, comment and category type. Read-only."),
                 noArgs, VibeCutToolRisk::ReadOnly, false, listGuides, error)) return false;

    const QJsonObject common{{QStringLiteral("frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                             {QStringLiteral("comment"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                             {QStringLiteral("type"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                                  {QStringLiteral("description"), QStringLiteral("Optional Kdenlive guide category/type; omit for default.")}}}};
    if (!addTool(surface, QStringLiteral("guide_add"),
                 QStringLiteral("Add or update an undoable project guide at an exact timeline frame and verify it in the live guide model."),
                 objectSchema(common, QJsonArray{QStringLiteral("frame"), QStringLiteral("comment")}),
                 VibeCutToolRisk::ReversibleEdit, true, [](const QJsonObject &input) { return addGuide(input, false); }, error)) return false;

    QJsonObject rangeProps = common;
    rangeProps.insert(QStringLiteral("duration_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}});
    if (!addTool(surface, QStringLiteral("guide_range_add"),
                 QStringLiteral("Add or update an undoable range guide for a candidate cut, B-roll span, review region, or other time-ranged annotation."),
                 objectSchema(rangeProps, QJsonArray{QStringLiteral("frame"), QStringLiteral("duration_frames"), QStringLiteral("comment")}),
                 VibeCutToolRisk::ReversibleEdit, true, [](const QJsonObject &input) { return addGuide(input, true); }, error)) return false;

    return addTool(surface, QStringLiteral("guide_remove"),
                   QStringLiteral("Remove one project guide at an exact frame using Kdenlive's undoable guide model and verify removal."),
                   objectSchema(QJsonObject{{QStringLiteral("frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}}},
                                QJsonArray{QStringLiteral("frame")}),
                   VibeCutToolRisk::ReversibleEdit, true, removeGuide, error);
}
