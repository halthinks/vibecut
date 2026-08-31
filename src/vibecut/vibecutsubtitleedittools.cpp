/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutsubtitleedittools.h"

#include "bin/model/subtitlemodel.hpp"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

namespace {
QJsonObject err(const QString &message) { return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}}; }

std::shared_ptr<TimelineItemModel> currentModel()
{
    if (!pCore || !pCore->window()) return nullptr;
    TimelineWidget *timeline = pCore->window()->getCurrentTimeline();
    return timeline ? timeline->model() : nullptr;
}

QJsonObject editSubtitle(const QJsonObject &input)
{
    const int id = input.value(QStringLiteral("subtitle_id")).toInt(-1);
    const QString text = input.value(QStringLiteral("text")).toString();
    if (id < 0) return err(QStringLiteral("subtitle_id must be >= 0"));
    if (text.trimmed().isEmpty()) return err(QStringLiteral("text must not be empty"));
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model || !model->hasSubtitleModel()) return err(QStringLiteral("No subtitle track is available on the active timeline."));
    const std::shared_ptr<SubtitleModel> subtitles = model->getSubtitleModel();
    if (!subtitles || !subtitles->hasSubtitle(id)) return err(QStringLiteral("Subtitle id %1 does not exist.").arg(id));
    const QString oldText = subtitles->getText(id);
    if (!subtitles->editSubtitle(id, text)) return err(QStringLiteral("Kdenlive rejected editing subtitle %1.").arg(id));
    if (subtitles->getText(id) != text) return err(QStringLiteral("Subtitle edit returned success but live subtitle text did not match."));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("subtitle_id"), id},
                       {QStringLiteral("old_text"), oldText}, {QStringLiteral("text"), text}, {QStringLiteral("verified"), true}};
}

QJsonObject deleteSubtitle(const QJsonObject &input)
{
    const int id = input.value(QStringLiteral("subtitle_id")).toInt(-1);
    if (id < 0) return err(QStringLiteral("subtitle_id must be >= 0"));
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model || !model->hasSubtitleModel()) return err(QStringLiteral("No subtitle track is available on the active timeline."));
    const std::shared_ptr<SubtitleModel> subtitles = model->getSubtitleModel();
    if (!subtitles || !subtitles->hasSubtitle(id)) return err(QStringLiteral("Subtitle id %1 does not exist.").arg(id));
    const QString oldText = subtitles->getText(id);
    if (!subtitles->removeSubtitle(id)) return err(QStringLiteral("Kdenlive rejected deleting subtitle %1.").arg(id));
    if (subtitles->hasSubtitle(id)) return err(QStringLiteral("Subtitle deletion returned success but id %1 is still present.").arg(id));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("deleted_subtitle_id"), id},
                       {QStringLiteral("old_text"), oldText}, {QStringLiteral("verified"), true}};
}

bool add(VibeCutToolSurface &surface, const QString &name, const QString &description, const VibeCutToolSurface::Handler &handler, QString *error)
{
    const QJsonObject properties = name == QLatin1String("subtitle_edit")
        ? QJsonObject{{QStringLiteral("subtitle_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                      {QStringLiteral("text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}
        : QJsonObject{{QStringLiteral("subtitle_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}}};
    const QJsonArray required = name == QLatin1String("subtitle_edit")
        ? QJsonArray{QStringLiteral("subtitle_id"), QStringLiteral("text")}
        : QJsonArray{QStringLiteral("subtitle_id")};
    const QJsonObject schema{{QStringLiteral("name"), name}, {QStringLiteral("description"), description},
                             {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                         {QStringLiteral("properties"), properties},
                                                                         {QStringLiteral("required"), required},
                                                                         {QStringLiteral("additionalProperties"), false}}}};
    VibeCutToolPolicy policy;
    policy.name = name;
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    return surface.registerTool(schema, policy, handler, error);
}
} // namespace

bool registerVibeCutSubtitleEditTools(VibeCutToolSurface &surface, QString *error)
{
    if (!add(surface, QStringLiteral("subtitle_edit"),
             QStringLiteral("Replace one existing subtitle's text by stable subtitle id and verify the live SubtitleModel value."),
             editSubtitle, error)) return false;
    return add(surface, QStringLiteral("subtitle_delete"),
               QStringLiteral("Delete one existing subtitle by stable subtitle id and verify removal. Undoable through Kdenlive."),
               deleteSubtitle, error);
}
