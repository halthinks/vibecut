/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuttitletools.h"

#include "bin/clipcreator.hpp"
#include "bin/projectclip.h"
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

QDomDocument buildSimpleTitleXml(const QString &text, int duration, int x, int y, int fontSize,
                                 const QColor &color, bool bold, bool italic)
{
    const QSize frameSize = pCore ? pCore->getCurrentFrameSize() : QSize();
    if (frameSize.width() <= 0 || frameSize.height() <= 0) return QDomDocument();

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
    if (!titleXml.documentElement().isNull()) {
        titleXml.documentElement().setAttribute(QStringLiteral("duration"), duration);
        titleXml.documentElement().setAttribute(QStringLiteral("out"), duration);
    }
    return titleXml;
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

    QDomDocument titleXml = buildSimpleTitleXml(text, duration, x, y, fontSize, color, bold, italic);
    if (titleXml.documentElement().isNull()) return err(QStringLiteral("Could not serialize the Kdenlive title document."));

    std::unordered_map<QString, QString> properties;
    properties[QStringLiteral("xmldata")] = titleXml.toString();
    properties[QStringLiteral("vibecut:simple_title")] = QStringLiteral("1");
    const QString binId = ClipCreator::createTitleClip(properties, duration, name, QStringLiteral("-1"), binModel);
    if (binId.isEmpty() || binId == QLatin1String("-1") || !binModel->getItemByBinId(binId)) {
        return err(QStringLiteral("Kdenlive could not create the title bin asset."));
    }

    int clipId = -1;
    if (!timeline->requestClipInsertion(binId, trackId, position, clipId, true, true, true)) {
        return err(QStringLiteral("The title asset was created but Kdenlive rejected inserting it on track %1 at frame %2; the current VibeCut checkpoint should roll the title creation back.")
                       .arg(trackId).arg(position));
    }
    const bool verified = timeline->isClip(clipId) && timeline->getClipBinId(clipId) == binId &&
                          timeline->getClipTrackId(clipId) == trackId && timeline->getClipPosition(clipId) == position;
    if (!verified) return err(QStringLiteral("Title insertion returned success but live timeline state did not match the created title asset."));

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("bin_id"), binId}, {QStringLiteral("clip_id"), clipId},
                       {QStringLiteral("track_id"), trackId}, {QStringLiteral("position_frame"), position},
                       {QStringLiteral("duration_frames"), timeline->getClipPlaytime(clipId)}, {QStringLiteral("name"), name},
                       {QStringLiteral("text"), text}, {QStringLiteral("simple_title"), true}, {QStringLiteral("verified"), true}};
}

QJsonObject updateTitle(const QJsonObject &input)
{
    if (!pCore) return err(QStringLiteral("Kdenlive core is unavailable."));
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    const QString text = input.value(QStringLiteral("text")).toString();
    const int x = input.value(QStringLiteral("x")).toInt(80);
    const int y = input.value(QStringLiteral("y")).toInt(80);
    const int fontSize = input.value(QStringLiteral("font_size")).toInt(64);
    const QString colorText = input.value(QStringLiteral("color")).toString(QStringLiteral("#ffffff"));
    const bool bold = input.value(QStringLiteral("bold")).toBool(false);
    const bool italic = input.value(QStringLiteral("italic")).toBool(false);
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty"));
    if (text.trimmed().isEmpty()) return err(QStringLiteral("text must not be empty"));
    if (fontSize <= 0) return err(QStringLiteral("font_size must be > 0"));
    QColor color(colorText);
    if (!color.isValid()) return err(QStringLiteral("color must be a valid Qt color string such as #ffffff"));

    const std::shared_ptr<ProjectItemModel> binModel = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> clip = binModel ? binModel->getClipByBinID(binId) : nullptr;
    if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    if (clip->clipType() != ClipType::Text) return err(QStringLiteral("Bin clip '%1' is not a Kdenlive title clip.").arg(binId));
    if (clip->getProducerProperty(QStringLiteral("vibecut:simple_title")) != QLatin1String("1")) {
        return err(QStringLiteral("title_update only edits VibeCut-created simple titles; arbitrary complex Kdenlive title layouts are protected from replacement."));
    }

    const int duration = qMax(1, clip->getFramePlaytime());
    QDomDocument titleXml = buildSimpleTitleXml(text, duration, x, y, fontSize, color, bold, italic);
    if (titleXml.documentElement().isNull()) return err(QStringLiteral("Could not serialize the updated Kdenlive title document."));
    const QString oldXml = clip->getProducerProperty(QStringLiteral("xmldata"));
    const QString newXml = titleXml.toString();
    if (oldXml == newXml) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("bin_id"), binId}, {QStringLiteral("text"), text},
                           {QStringLiteral("changed"), false}, {QStringLiteral("verified"), true}};
    }

    clip->setProducerProperty(QStringLiteral("xmldata"), newXml);
    clip->reloadTimeline();
    if (clip->getProducerProperty(QStringLiteral("xmldata")) != newXml) {
        clip->setProducerProperty(QStringLiteral("xmldata"), oldXml);
        clip->reloadTimeline();
        return err(QStringLiteral("Title update did not verify on the live producer."));
    }

    const std::shared_ptr<ProjectClip> retained = clip;
    Fun undo = [retained, oldXml]() {
        retained->setProducerProperty(QStringLiteral("xmldata"), oldXml);
        retained->reloadTimeline();
        return retained->getProducerProperty(QStringLiteral("xmldata")) == oldXml;
    };
    Fun redo = [retained, newXml]() {
        retained->setProducerProperty(QStringLiteral("xmldata"), newXml);
        retained->reloadTimeline();
        return retained->getProducerProperty(QStringLiteral("xmldata")) == newXml;
    };
    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: update title"));

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("bin_id"), binId}, {QStringLiteral("text"), text},
                       {QStringLiteral("duration_frames"), duration}, {QStringLiteral("changed"), true},
                       {QStringLiteral("verified"), true}};
}

QJsonObject styleProperties()
{
    return QJsonObject{{QStringLiteral("x"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                       {QStringLiteral("y"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                       {QStringLiteral("font_size"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}},
                       {QStringLiteral("color"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                       {QStringLiteral("bold"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
                       {QStringLiteral("italic"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}};
}

bool registerEditTool(VibeCutToolSurface &surface, const QString &name, const QString &description, const QJsonObject &schema,
                      const VibeCutToolSurface::Handler &handler, QString *error)
{
    VibeCutToolPolicy policy;
    policy.name = name;
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("description"), description},
                                            {QStringLiteral("input_schema"), schema}}, policy, handler, error);
}
} // namespace

bool registerVibeCutTitleTools(VibeCutToolSurface &surface, QString *error)
{
    QJsonObject createProperties = styleProperties();
    createProperties.insert(QStringLiteral("text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
    createProperties.insert(QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
    createProperties.insert(QStringLiteral("track_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}});
    createProperties.insert(QStringLiteral("position_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}});
    createProperties.insert(QStringLiteral("duration_frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}});
    const QJsonObject createInput{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), createProperties},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("text"), QStringLiteral("track_id"), QStringLiteral("position_frame"), QStringLiteral("duration_frames")}},
                                  {QStringLiteral("additionalProperties"), false}};
    if (!registerEditTool(surface, QStringLiteral("title_create"),
                          QStringLiteral("Create a real Kdenlive simple-title bin asset from text using TitleDocument/ClipCreator, insert it on an exact video track/frame, and verify both bin and timeline state. The asset is marked as VibeCut-simple so later title_update can safely replace its single text element."),
                          createInput, createTitle, error)) return false;

    QJsonObject updateProperties = styleProperties();
    updateProperties.insert(QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
    updateProperties.insert(QStringLiteral("text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
    const QJsonObject updateInput{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), updateProperties},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id"), QStringLiteral("text")}},
                                  {QStringLiteral("additionalProperties"), false}};
    return registerEditTool(surface, QStringLiteral("title_update"),
                            QStringLiteral("Replace the text/style of a VibeCut-created simple Kdenlive title bin asset, reload every timeline instance, verify the producer property, and create undo/redo. Refuses arbitrary complex Kdenlive titles to avoid destroying hand-built layouts."),
                            updateInput, updateTitle, error);
}
