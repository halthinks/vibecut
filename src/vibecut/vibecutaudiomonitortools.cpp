/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutaudiomonitortools.h"

#include "audiomixer/mixermanager.hpp"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "vibecuttoolsurface.h"

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

QJsonObject status(const QJsonObject &)
{
    if (!pCore || !pCore->mixer()) return err(QStringLiteral("Kdenlive audio mixer is unavailable."));
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    const int trackId = pCore->mixer()->recordTrack();
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("monitored_record_track_id"), trackId},
                       {QStringLiteral("track_exists"), model ? model->isTrack(trackId) : false},
                       {QStringLiteral("audio_track"), model && model->isTrack(trackId) ? model->isAudioTrack(trackId) : false}};
}

QJsonObject setMonitor(const QJsonObject &input)
{
    if (!pCore || !pCore->mixer()) return err(QStringLiteral("Kdenlive audio mixer is unavailable."));
    const std::shared_ptr<TimelineItemModel> model = currentModel();
    if (!model) return err(QStringLiteral("No timeline is open."));
    const int trackId = input.value(QStringLiteral("track_id")).toInt(-1);
    const bool enabled = input.value(QStringLiteral("enabled")).toBool();
    if (!model->isTrack(trackId) || !model->isAudioTrack(trackId)) {
        return err(QStringLiteral("track_id %1 must be an existing audio track.").arg(trackId));
    }

    pCore->mixer()->monitorAudio(trackId, enabled);
    const int liveTrack = pCore->mixer()->recordTrack();
    if (enabled && liveTrack != trackId) {
        return err(QStringLiteral("Kdenlive did not verify audio monitoring on track %1.").arg(trackId));
    }
    if (!enabled && liveTrack == trackId) {
        return err(QStringLiteral("Kdenlive did not verify audio monitoring was disabled on track %1.").arg(trackId));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("track_id"), trackId},
                       {QStringLiteral("enabled"), enabled}, {QStringLiteral("verified"), true}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required},
                       {QStringLiteral("additionalProperties"), false}};
}
} // namespace

bool registerVibeCutAudioMonitorTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    const QJsonObject statusSchema{{QStringLiteral("name"), QStringLiteral("audio_monitor_status")},
                                   {QStringLiteral("description"), QStringLiteral("Inspect the audio track currently monitored by Kdenlive's mixer for recording/monitoring. Ephemeral editor state; read-only." )},
                                   {QStringLiteral("input_schema"), noArgs}};
    VibeCutToolPolicy statusPolicy;
    statusPolicy.name = QStringLiteral("audio_monitor_status");
    statusPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(statusSchema, statusPolicy, status, error)) return false;

    const QJsonObject setInput = objectSchema(
        QJsonObject{{QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                    {QStringLiteral("enabled"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}},
        QJsonArray{QStringLiteral("track_id"), QStringLiteral("enabled")});
    const QJsonObject setSchema{{QStringLiteral("name"), QStringLiteral("audio_monitor_set")},
                                {QStringLiteral("description"), QStringLiteral("Enable or disable Kdenlive mixer monitoring for one existing audio track and verify the live mixer state. This is ephemeral editor state, not a project mutation." )},
                                {QStringLiteral("input_schema"), setInput}};
    VibeCutToolPolicy setPolicy;
    setPolicy.name = QStringLiteral("audio_monitor_set");
    setPolicy.risk = VibeCutToolRisk::ReadOnly;
    setPolicy.mutatesProject = false;
    return surface.registerTool(setSchema, setPolicy, setMonitor, error);
}
