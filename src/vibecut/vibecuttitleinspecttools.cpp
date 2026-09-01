/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuttitleinspecttools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "vibecuttoolsurface.h"

#include <QDomDocument>
#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

std::shared_ptr<ProjectClip> titleClip(const QString &binId, bool requireEditableXml, QJsonObject &failure)
{
    if (!pCore) {
        failure = err(QStringLiteral("Kdenlive core is unavailable."));
        return nullptr;
    }
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip) {
        failure = err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
        return nullptr;
    }
    if (clip->clipType() != ClipType::Text && clip->clipType() != ClipType::TextTemplate) {
        failure = err(QStringLiteral("Bin clip '%1' is not a Kdenlive title/title-template asset.").arg(binId));
        return nullptr;
    }
    if (requireEditableXml && clip->clipType() != ClipType::Text) {
        failure = err(QStringLiteral("Element editing currently supports embedded Kdenlive title clips only, not external text-template assets."));
        return nullptr;
    }
    return clip;
}

bool parseXml(const QString &xml, QDomDocument &document, QString &error)
{
    if (xml.trimmed().isEmpty()) {
        error = QStringLiteral("Title asset has no embedded xmldata.");
        return false;
    }
    const QDomDocument::ParseResult parsed = document.setContent(xml);
    if (!parsed) {
        error = QStringLiteral("Title xmldata could not be parsed at line %1, column %2: %3")
                    .arg(parsed.errorLine).arg(parsed.errorColumn).arg(parsed.errorMessage);
        return false;
    }
    if (document.documentElement().tagName() != QLatin1String("kdenlivetitle")) {
        error = QStringLiteral("Embedded xmldata is not a kdenlivetitle document.");
        return false;
    }
    return true;
}

QJsonObject itemSummary(const QDomElement &item, int itemIndex, int textIndex)
{
    QJsonObject result{{QStringLiteral("item_index"), itemIndex},
                       {QStringLiteral("type"), item.attribute(QStringLiteral("type"))},
                       {QStringLiteral("z_index"), item.attribute(QStringLiteral("z-index")).toInt()}};
    if (textIndex >= 0) result.insert(QStringLiteral("text_item_index"), textIndex);

    const QDomElement position = item.firstChildElement(QStringLiteral("position"));
    if (!position.isNull()) {
        result.insert(QStringLiteral("x"), position.attribute(QStringLiteral("x")).toDouble());
        result.insert(QStringLiteral("y"), position.attribute(QStringLiteral("y")).toDouble());
        const QString transform = position.firstChildElement(QStringLiteral("transform")).text();
        if (!transform.isEmpty()) result.insert(QStringLiteral("transform"), transform);
    }

    const QDomElement content = item.firstChildElement(QStringLiteral("content"));
    if (!content.isNull()) {
        result.insert(QStringLiteral("text"), content.text());
        QJsonObject attributes;
        const QDomNamedNodeMap attrs = content.attributes();
        for (int i = 0; i < attrs.count(); ++i) {
            const QDomNode attr = attrs.item(i);
            attributes.insert(attr.nodeName(), attr.nodeValue());
        }
        result.insert(QStringLiteral("content_attributes"), attributes);
    }
    return result;
}

QJsonObject inspectTitle(const QJsonObject &input)
{
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty."));

    QJsonObject failure;
    const std::shared_ptr<ProjectClip> clip = titleClip(binId, false, failure);
    if (!clip) return failure;

    const QString xml = clip->getProducerProperty(QStringLiteral("xmldata"));
    QDomDocument document;
    QString parseError;
    if (!parseXml(xml, document, parseError)) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("bin_id"), binId},
                           {QStringLiteral("clip_type"), static_cast<int>(clip->clipType())},
                           {QStringLiteral("embedded_xml"), false}, {QStringLiteral("parse_error"), parseError},
                           {QStringLiteral("raw_xml"), xml}};
    }

    const QDomElement root = document.documentElement();
    const QDomNodeList nodes = root.elementsByTagName(QStringLiteral("item"));
    QJsonArray items;
    int textIndex = 0;
    for (int i = 0; i < nodes.count(); ++i) {
        const QDomElement item = nodes.at(i).toElement();
        const bool isText = item.attribute(QStringLiteral("type")) == QLatin1String("QGraphicsTextItem");
        items.append(itemSummary(item, i, isText ? textIndex++ : -1));
    }

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("bin_id"), binId},
                       {QStringLiteral("clip_type"), static_cast<int>(clip->clipType())},
                       {QStringLiteral("embedded_xml"), true}, {QStringLiteral("width"), root.attribute(QStringLiteral("width")).toInt()},
                       {QStringLiteral("height"), root.attribute(QStringLiteral("height")).toInt()},
                       {QStringLiteral("duration_frames"), clip->getFramePlaytime()},
                       {QStringLiteral("item_count"), nodes.count()}, {QStringLiteral("text_item_count"), textIndex},
                       {QStringLiteral("items"), items}, {QStringLiteral("raw_xml"), xml},
                       {QStringLiteral("vibecut_simple_title"), clip->getProducerProperty(QStringLiteral("vibecut:simple_title")) == QLatin1String("1")}};
}

QJsonObject setTextItem(const QJsonObject &input)
{
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    const int wantedIndex = input.value(QStringLiteral("text_item_index")).toInt(-1);
    const QString text = input.value(QStringLiteral("text")).toString();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty."));
    if (wantedIndex < 0) return err(QStringLiteral("text_item_index must be >= 0."));

    QJsonObject failure;
    const std::shared_ptr<ProjectClip> clip = titleClip(binId, true, failure);
    if (!clip) return failure;

    const QString oldXml = clip->getProducerProperty(QStringLiteral("xmldata"));
    QDomDocument document;
    QString parseError;
    if (!parseXml(oldXml, document, parseError)) return err(parseError);

    QDomNodeList nodes = document.documentElement().elementsByTagName(QStringLiteral("item"));
    QDomElement target;
    int textIndex = 0;
    for (int i = 0; i < nodes.count(); ++i) {
        QDomElement item = nodes.at(i).toElement();
        if (item.attribute(QStringLiteral("type")) != QLatin1String("QGraphicsTextItem")) continue;
        if (textIndex == wantedIndex) {
            target = item;
            break;
        }
        ++textIndex;
    }
    if (target.isNull()) return err(QStringLiteral("Title has no text item at index %1. Call title_inspect first.").arg(wantedIndex));
    QDomElement content = target.firstChildElement(QStringLiteral("content"));
    if (content.isNull()) return err(QStringLiteral("Selected title text item has no content node."));

    const QString oldText = content.text();
    if (oldText == text) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("bin_id"), binId},
                           {QStringLiteral("text_item_index"), wantedIndex}, {QStringLiteral("old_text"), oldText},
                           {QStringLiteral("text"), text}, {QStringLiteral("changed"), false}, {QStringLiteral("verified"), true}};
    }

    while (!content.firstChild().isNull()) content.removeChild(content.firstChild());
    content.appendChild(document.createTextNode(text));
    const QString newXml = document.toString();

    clip->setProducerProperty(QStringLiteral("xmldata"), newXml);
    clip->reloadTimeline();
    if (clip->getProducerProperty(QStringLiteral("xmldata")) != newXml) {
        clip->setProducerProperty(QStringLiteral("xmldata"), oldXml);
        clip->reloadTimeline();
        return err(QStringLiteral("Title text-item update did not verify on the live producer."));
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
    pCore->pushUndo(undo, redo, QStringLiteral("VibeCut: edit title text element"));

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("bin_id"), binId},
                       {QStringLiteral("text_item_index"), wantedIndex}, {QStringLiteral("old_text"), oldText},
                       {QStringLiteral("text"), text}, {QStringLiteral("changed"), true}, {QStringLiteral("verified"), true}};
}
} // namespace

bool registerVibeCutTitleInspectTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject inspectInput{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), QJsonObject{{QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                                   {QStringLiteral("additionalProperties"), false}};
    const QJsonObject inspectSchema{{QStringLiteral("name"), QStringLiteral("title_inspect")},
                                    {QStringLiteral("description"), QStringLiteral("Inspect any Kdenlive title/title-template bin asset without mutation. For embedded title XML, enumerate every item with stable item/text indexes, type, position, text/content attributes, dimensions and raw XML so complex layouts can be reasoned about before editing.")},
                                    {QStringLiteral("input_schema"), inspectInput}};
    VibeCutToolPolicy inspectPolicy;
    inspectPolicy.name = QStringLiteral("title_inspect");
    inspectPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(inspectSchema, inspectPolicy, inspectTitle, error)) return false;

    const QJsonObject setInput{{QStringLiteral("type"), QStringLiteral("object")},
                               {QStringLiteral("properties"), QJsonObject{
                                   {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                   {QStringLiteral("text_item_index"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                   {QStringLiteral("text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                               {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id"), QStringLiteral("text_item_index"), QStringLiteral("text")}},
                               {QStringLiteral("additionalProperties"), false}};
    const QJsonObject setSchema{{QStringLiteral("name"), QStringLiteral("title_text_item_set")},
                                {QStringLiteral("description"), QStringLiteral("Change only the text content of one indexed QGraphicsTextItem inside an embedded Kdenlive title. Preserves all unrelated title XML/layout, reloads every timeline instance, verifies the exact producer XML, and creates undo/redo. Call title_inspect first.")},
                                {QStringLiteral("input_schema"), setInput}};
    VibeCutToolPolicy setPolicy;
    setPolicy.name = QStringLiteral("title_text_item_set");
    setPolicy.risk = VibeCutToolRisk::ReversibleEdit;
    setPolicy.reversible = true;
    setPolicy.mutatesProject = true;
    return surface.registerTool(setSchema, setPolicy, setTextItem, error);
}
