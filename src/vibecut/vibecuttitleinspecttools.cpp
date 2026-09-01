/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuttitleinspecttools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "vibecuttoolsurface.h"

#include <QColor>
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

QDomElement textItemAt(QDomDocument &document, int wantedIndex)
{
    QDomNodeList nodes = document.documentElement().elementsByTagName(QStringLiteral("item"));
    int textIndex = 0;
    for (int i = 0; i < nodes.count(); ++i) {
        QDomElement item = nodes.at(i).toElement();
        if (item.attribute(QStringLiteral("type")) != QLatin1String("QGraphicsTextItem")) continue;
        if (textIndex == wantedIndex) return item;
        ++textIndex;
    }
    return QDomElement();
}

QString kdenliveColor(const QColor &color)
{
    return QStringLiteral("%1,%2,%3,%4").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}

QJsonObject applyTitleXml(const std::shared_ptr<ProjectClip> &clip, const QString &oldXml, const QString &newXml, const QString &undoText)
{
    if (oldXml == newXml) return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("changed"), false}, {QStringLiteral("verified"), true}};
    clip->setProducerProperty(QStringLiteral("xmldata"), newXml);
    clip->reloadTimeline();
    if (clip->getProducerProperty(QStringLiteral("xmldata")) != newXml) {
        clip->setProducerProperty(QStringLiteral("xmldata"), oldXml);
        clip->reloadTimeline();
        return err(QStringLiteral("Title XML update did not verify on the live producer."));
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
    pCore->pushUndo(undo, redo, undoText);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("changed"), true}, {QStringLiteral("verified"), true}};
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
    QDomElement target = textItemAt(document, wantedIndex);
    if (target.isNull()) return err(QStringLiteral("Title has no text item at index %1. Call title_inspect first.").arg(wantedIndex));
    QDomElement content = target.firstChildElement(QStringLiteral("content"));
    if (content.isNull()) return err(QStringLiteral("Selected title text item has no content node."));

    const QString oldText = content.text();
    while (!content.firstChild().isNull()) content.removeChild(content.firstChild());
    content.appendChild(document.createTextNode(text));
    const QJsonObject applied = applyTitleXml(clip, oldXml, document.toString(), QStringLiteral("VibeCut: edit title text element"));
    if (!applied.value(QStringLiteral("ok")).toBool()) return applied;
    QJsonObject result = applied;
    result.insert(QStringLiteral("bin_id"), binId);
    result.insert(QStringLiteral("text_item_index"), wantedIndex);
    result.insert(QStringLiteral("old_text"), oldText);
    result.insert(QStringLiteral("text"), text);
    return result;
}

QJsonObject setTextItemStyle(const QJsonObject &input)
{
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    const int wantedIndex = input.value(QStringLiteral("text_item_index")).toInt(-1);
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty."));
    if (wantedIndex < 0) return err(QStringLiteral("text_item_index must be >= 0."));

    const QStringList editable = {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("font_family"), QStringLiteral("font_pixel_size"),
                                  QStringLiteral("font_color"), QStringLiteral("font_weight"), QStringLiteral("italic"), QStringLiteral("z_index")};
    bool any = false;
    for (const QString &key : editable) any = any || input.contains(key);
    if (!any) return err(QStringLiteral("Specify at least one style/position field to change."));

    QJsonObject failure;
    const std::shared_ptr<ProjectClip> clip = titleClip(binId, true, failure);
    if (!clip) return failure;
    const QString oldXml = clip->getProducerProperty(QStringLiteral("xmldata"));
    QDomDocument document;
    QString parseError;
    if (!parseXml(oldXml, document, parseError)) return err(parseError);
    QDomElement target = textItemAt(document, wantedIndex);
    if (target.isNull()) return err(QStringLiteral("Title has no text item at index %1. Call title_inspect first.").arg(wantedIndex));
    QDomElement content = target.firstChildElement(QStringLiteral("content"));
    QDomElement position = target.firstChildElement(QStringLiteral("position"));
    if (content.isNull() || position.isNull()) return err(QStringLiteral("Selected title text item is missing content or position metadata."));

    if (input.contains(QStringLiteral("x"))) position.setAttribute(QStringLiteral("x"), input.value(QStringLiteral("x")).toDouble());
    if (input.contains(QStringLiteral("y"))) position.setAttribute(QStringLiteral("y"), input.value(QStringLiteral("y")).toDouble());
    if (input.contains(QStringLiteral("font_family"))) {
        const QString family = input.value(QStringLiteral("font_family")).toString().trimmed();
        if (family.isEmpty()) return err(QStringLiteral("font_family must not be empty."));
        content.setAttribute(QStringLiteral("font"), family);
    }
    if (input.contains(QStringLiteral("font_pixel_size"))) {
        const int size = input.value(QStringLiteral("font_pixel_size")).toInt(-1);
        if (size <= 0) return err(QStringLiteral("font_pixel_size must be > 0."));
        content.setAttribute(QStringLiteral("font-pixel-size"), size);
    }
    if (input.contains(QStringLiteral("font_color"))) {
        const QColor color(input.value(QStringLiteral("font_color")).toString());
        if (!color.isValid()) return err(QStringLiteral("font_color must be a valid Qt color such as #ffffff."));
        content.setAttribute(QStringLiteral("font-color"), kdenliveColor(color));
    }
    if (input.contains(QStringLiteral("font_weight"))) {
        const int weight = input.value(QStringLiteral("font_weight")).toInt(-1);
        if (weight < 0 || weight > 99) return err(QStringLiteral("font_weight must be between 0 and 99."));
        content.setAttribute(QStringLiteral("font-weight"), weight);
    }
    if (input.contains(QStringLiteral("italic"))) content.setAttribute(QStringLiteral("font-italic"), input.value(QStringLiteral("italic")).toBool() ? 1 : 0);
    if (input.contains(QStringLiteral("z_index"))) target.setAttribute(QStringLiteral("z-index"), input.value(QStringLiteral("z_index")).toInt());

    const QJsonObject applied = applyTitleXml(clip, oldXml, document.toString(), QStringLiteral("VibeCut: edit title text style"));
    if (!applied.value(QStringLiteral("ok")).toBool()) return applied;
    QJsonObject result = applied;
    result.insert(QStringLiteral("bin_id"), binId);
    result.insert(QStringLiteral("text_item_index"), wantedIndex);
    result.insert(QStringLiteral("item"), itemSummary(target, -1, wantedIndex));
    return result;
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
    if (!surface.registerTool(setSchema, setPolicy, setTextItem, error)) return false;

    const QJsonObject styleInput{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), QJsonObject{
                                     {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                     {QStringLiteral("text_item_index"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                     {QStringLiteral("x"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                                     {QStringLiteral("y"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                                     {QStringLiteral("font_family"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                     {QStringLiteral("font_pixel_size"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}},
                                     {QStringLiteral("font_color"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                     {QStringLiteral("font_weight"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 99}}},
                                     {QStringLiteral("italic"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
                                     {QStringLiteral("z_index"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}}},
                                 {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id"), QStringLiteral("text_item_index")}},
                                 {QStringLiteral("additionalProperties"), false}};
    const QJsonObject styleSchema{{QStringLiteral("name"), QStringLiteral("title_text_item_style_set")},
                                  {QStringLiteral("description"), QStringLiteral("Edit only selected position/font/color/weight/italic/z-index attributes of one indexed title text element while preserving all unrelated Kdenlive title XML. Reloads every timeline instance, verifies exact XML, and creates one undo command. Call title_inspect first.")},
                                  {QStringLiteral("input_schema"), styleInput}};
    VibeCutToolPolicy stylePolicy;
    stylePolicy.name = QStringLiteral("title_text_item_style_set");
    stylePolicy.risk = VibeCutToolRisk::ReversibleEdit;
    stylePolicy.reversible = true;
    stylePolicy.mutatesProject = true;
    return surface.registerTool(styleSchema, stylePolicy, setTextItemStyle, error);
}
