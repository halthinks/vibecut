/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutsequencetools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "vibecuttoolsurface.h"

#include <QJsonArray>
#include <QSet>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QJsonObject listSequences(const QJsonObject &)
{
    if (!pCore || !pCore->currentDoc()) return err(QStringLiteral("No project document is open."));
    const std::shared_ptr<ProjectItemModel> bin = pCore->projectItemModel();
    if (!bin) return err(QStringLiteral("Project bin model is unavailable."));

    const QMap<QUuid, QString> sequenceMap = bin->getAllSequenceClips();
    const QList<QUuid> openedList = pCore->currentDoc()->getTimelinesUuids();
    QSet<QUuid> opened(openedList.begin(), openedList.end());
    const QUuid active = pCore->currentDoc()->activeUuid;

    QJsonArray sequences;
    for (auto it = sequenceMap.constBegin(); it != sequenceMap.constEnd(); ++it) {
        const QUuid uuid = it.key();
        const QString binId = it.value();
        const std::shared_ptr<ProjectClip> clip = bin->getClipByBinID(binId);
        QJsonObject item{{QStringLiteral("uuid"), uuid.toString(QUuid::WithoutBraces)},
                         {QStringLiteral("bin_id"), binId},
                         {QStringLiteral("active"), uuid == active},
                         {QStringLiteral("opened"), opened.contains(uuid)}};
        if (clip) {
            item.insert(QStringLiteral("name"), clip->clipName());
            item.insert(QStringLiteral("duration_frames"), clip->getFramePlaytime());
            item.insert(QStringLiteral("timeline_instances"), static_cast<int>(clip->timelineInstances().size()));
        }
        sequences.append(item);
    }

    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("sequence_count"), sequences.size()},
                       {QStringLiteral("active_uuid"), active.toString(QUuid::WithoutBraces)},
                       {QStringLiteral("sequences"), sequences}};
}

QJsonObject inspectSequence(const QJsonObject &input)
{
    if (!pCore || !pCore->currentDoc()) return err(QStringLiteral("No project document is open."));
    const QString text = input.value(QStringLiteral("uuid")).toString().trimmed();
    const QUuid uuid(text);
    if (uuid.isNull()) return err(QStringLiteral("uuid must be a valid sequence UUID."));
    const std::shared_ptr<ProjectItemModel> bin = pCore->projectItemModel();
    if (!bin) return err(QStringLiteral("Project bin model is unavailable."));
    const QMap<QUuid, QString> sequenceMap = bin->getAllSequenceClips();
    if (!sequenceMap.contains(uuid)) return err(QStringLiteral("Sequence UUID '%1' is not present in this project.").arg(text));

    const QString binId = sequenceMap.value(uuid);
    const std::shared_ptr<ProjectClip> clip = bin->getClipByBinID(binId);
    const QList<QUuid> opened = pCore->currentDoc()->getTimelinesUuids();
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("uuid"), uuid.toString(QUuid::WithoutBraces)},
                       {QStringLiteral("bin_id"), binId}, {QStringLiteral("active"), uuid == pCore->currentDoc()->activeUuid},
                       {QStringLiteral("opened"), opened.contains(uuid)}};
    if (clip) {
        result.insert(QStringLiteral("name"), clip->clipName());
        result.insert(QStringLiteral("duration_frames"), clip->getFramePlaytime());
        result.insert(QStringLiteral("timeline_instances"), static_cast<int>(clip->timelineInstances().size()));
        result.insert(QStringLiteral("description"), clip->getProducerProperty(QStringLiteral("kdenlive:description")));
        result.insert(QStringLiteral("tags"), clip->getProducerProperty(QStringLiteral("kdenlive:tags")));
        result.insert(QStringLiteral("rating"), clip->getProducerIntProperty(QStringLiteral("kdenlive:rating")));
    }
    const std::shared_ptr<TimelineItemModel> timeline = pCore->currentDoc()->getTimeline(uuid, true);
    if (timeline) {
        result.insert(QStringLiteral("track_count"), timeline->getTracksCount());
        result.insert(QStringLiteral("clip_count"), timeline->getClipsCount());
        result.insert(QStringLiteral("composition_count"), timeline->getCompositionsCount());
        result.insert(QStringLiteral("duration_frames_live"), timeline->duration());
        result.insert(QStringLiteral("loading"), timeline->isLoading);
    }
    return result;
}
} // namespace

bool registerVibeCutSequenceTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject noArgs{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"), QJsonObject{}},
                             {QStringLiteral("additionalProperties"), false}};
    const QJsonObject listSchema{{QStringLiteral("name"), QStringLiteral("sequences_list")},
                                 {QStringLiteral("description"), QStringLiteral("List every Kdenlive sequence/nested timeline in the current project with stable UUID, bin id, name, duration, active/open state and timeline usage. Read-only; prevents planning as though only the visible timeline exists.")},
                                 {QStringLiteral("input_schema"), noArgs}};
    VibeCutToolPolicy listPolicy;
    listPolicy.name = QStringLiteral("sequences_list");
    listPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(listSchema, listPolicy, listSequences, error)) return false;

    const QJsonObject inspectInput{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), QJsonObject{{QStringLiteral("uuid"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("uuid")}},
                                   {QStringLiteral("additionalProperties"), false}};
    const QJsonObject inspectSchema{{QStringLiteral("name"), QStringLiteral("sequence_inspect")},
                                    {QStringLiteral("description"), QStringLiteral("Inspect one Kdenlive sequence by stable UUID, including bin metadata plus live timeline track/clip/composition counts, duration and loading/open/active state. Read-only.")},
                                    {QStringLiteral("input_schema"), inspectInput}};
    VibeCutToolPolicy inspectPolicy;
    inspectPolicy.name = QStringLiteral("sequence_inspect");
    inspectPolicy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(inspectSchema, inspectPolicy, inspectSequence, error);
}
