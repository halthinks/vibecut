/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuttitletools.h"

#include "bin/clipcreator.hpp"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"
#include "titler/graphicsscenerectmove.h"
#include "titler/titledocument.h"
#include "vibecuttoolsurface.h"

#include <QColor>
#include <QFont>
#include <QJsonArray>

#include <unordered_map>

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

QJsonObject createTitle(const QJsonObject &input)
{
    const QString text = input.value(QStringLiteral("text")).toString();
    const QString name = input.value(QStringLiteral("name")).toString(QStringLiteral("VibeCut Title")).trimmed();
    const int trackId = input.value(QStringLiteral("track_id")).toInt(-1);
    const int position = input.value(QStringLiteral("position_frame")).toInt(-1);
    const int duration = input.value(QStringLiteral("duration_frames")).toInt(-1);
    const int x = input.value(QStringLiteral("x")).toInt(80);
    const int y = input.value(QStringLiteral("y")).toInt(80);
    const int fontSize = input.value(QStringLiteral("font_size")).toInt(64);
    const QString colorText = input.value(QStringLiteral("color")).toString(QStringLiteral("#ffffff"));
    const bool bold = input.value(QStringLiteral("bold")).toBool(false);
    const bool italic = input.value(QStringLiteral("italic")).toBool(false);

    if (text.trimmed().isEmpty()) return err(QStringLiteral("text must not be empty"));
    if (name.isEmpty()) return err(QStringLiteral("name must not be empty"));
    if (position < 0) return err(QStringLiteral("position_frame must be >= 0"));
    if (duration <= 0) return err(QStringLiteral("duration_frames must be > 0"));
    if (fontSize <= 0) return err(QStringLiteral("font_size must be > 0"));
    QColor color(colorText);
    if (!color.isValid()) return err(QStringLiteral("color must be a valid Qt color string such as #ffffff"));

    const std::shared_ptr<TimelineItemModel> timeline = currentModel();
    if (!timeline) return err(QStringLiteral("No timeline is open."));
    if (!timeline->isTrack(trackId) || timeline->isAudioTrack(trackId)) {
        return err(QStringLiteral("track_id %1 must be an existing video track.").arg(trackId));
    }
    const std::shared_ptr<ProjectItemModel> binModel = pCore->projectItemModel();
    if (!binModel) return err(QStringLiteral("Project bin model is unavailable."));

    const QSize frameSize = pCore->getCurrentFrameSize();
    if (frameSize.width() <= 0 || frameSize.height() <= 0) {
        return err(QStringLiteral("Project frame size is unavailable."));
    }

    MyTextItem *item = new MyTextItem(text, nullptr);
    item->setFlag(QGraphicsItem::ItemIsSelectable, true);
    item->setPos(x, y);
    item->setTextColor(color);
    QFont font = item->font();
    font.setPixelSize(fontSize);
    font.setBold(bold);
    font.setItalic(italic);
    item->setFont(font);

    QList<QGraphicsItem *> items;
    items.append(item);
    QDomDocument titleXml = TitleDocument::xml(items, frameSize.width(), frameSize.height(), nullptr, nullptr, false);
    delete item;
    if (titleXml.documentElement().isNull()) {
        return err(QStringLiteral("Could not serialize the Kdenlive title document."));
    }
    titleXml.documentElement().setAttribute(QStringLiteral("duration"), duration);
    titleXml.documentElement().setAttribute(QStringLiteral("out"), duration);

    std::unordered_map<QString, QString> properties;
    properties[QStringLiteral("xmldata")] = titleXml.toString();
    const QString binId = ClipCreator::createTitleClip(properties, duration, name, QStringLiteral("-1"), binModel);
    if (binId.isEmpty() || binId == QLatin1String("-1") || !binModel->hasClip(binId)) {
        return err(QStringLiteral("Kdenlive could not create the title bin asset."));
    }

    int clipId = -1;
    if (!timeline->requestClipInsertion(binId, trackId, position, clipId, true, true, true)) {
        return err(QStringLiteral("The title asset was created but Kdenlive rejected inserting it on track %1 at frame %2; the current VibeCut checkpoint should roll the title creation back.")
                       .arg(trackId).arg(position));
    }
    const bool verified = timeline->isClip(clipId) && timeline->getClipBinId(clipId) == binId &&
                          timeline->getClipTrackId(clipId) == trackId && timeline->getClipPosition(clipId) == position;
    if (!verified) {
        return err(QStringLiteral("Title insertion returned success but live timeline state did not match the created title asset."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("bin_id"), binId}, {QStringLiteral("clip_id"), clipId},
                       {QStringLiteral("track_id"), trackId}, {QStringLiteral("position_frame"), position},
                       {QStringLiteral("duration_frames"), timeline->getClipPlaytime(clipId)}, {QStringLiteral("name"), name},
                       {QStringLiteral("text"), text}, {QStringLiteral("verified"), true}};
}
} // namespace

bool registerVibeCutTitleTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject inputSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{
                                      {QStringLiteral("text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                      {QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                      {QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                      {QStringLiteral("position_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                      {QStringLiteral("duration_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}},
                                      {QStringLiteral("x"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                      {QStringLiteral("y"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                      {QStringLiteral("font_size"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}},
                                      {QStringLiteral("color"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                      {QStringLiteral("bold"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
                                      {QStringLiteral("italic"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("text"), QStringLiteral("track_id"), QStringLiteral("position_frame"), QStringLiteral("duration_frames")}},
                                  {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("title_create")},
                             {QStringLiteral("description"), QStringLiteral("Create a real Kdenlive title bin asset from text using TitleDocument/ClipCreator, insert it on an exact video track/frame, and verify both bin and timeline state. Undoable as one governed checkpoint.")},
                             {QStringLiteral("input_schema"), inputSchema}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("title_create");
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    return surface.registerTool(schema, policy, createTitle, error);
}
