/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutbinmetadatatools.h"

#include "bin/bin.h"
#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QJsonObject metadataFor(const QString &binId, const std::shared_ptr<ProjectClip> &clip)
{
    QJsonArray tags;
    const QStringList splitTags = clip->tags().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &tag : splitTags) tags.append(tag);
    return QJsonObject{{QStringLiteral("bin_id"), binId},
                       {QStringLiteral("name"), clip->clipName()},
                       {QStringLiteral("description"), clip->description()},
                       {QStringLiteral("tags"), tags},
                       {QStringLiteral("rating_raw_0_10"), static_cast<int>(clip->rating())},
                       {QStringLiteral("clip_type"), static_cast<int>(clip->clipType())},
                       {QStringLiteral("timeline_instances"), static_cast<int>(clip->timelineInstances().size())}};
}

QJsonObject getMetadata(const QJsonObject &input)
{
    if (!pCore) return err(QStringLiteral("Kdenlive core is unavailable."));
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty"));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) return err(QStringLiteral("Project bin model is unavailable."));
    const std::shared_ptr<ProjectClip> clip = model->getClipByBinID(binId);
    if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("metadata"), metadataFor(binId, clip)}};
}

QJsonObject setMetadata(const QJsonObject &input)
{
    if (!pCore || !pCore->bin()) return err(QStringLiteral("Kdenlive project bin is unavailable."));
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty"));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) return err(QStringLiteral("Project bin model is unavailable."));
    const std::shared_ptr<ProjectClip> clip = model->getClipByBinID(binId);
    if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));

    QMap<QString, QString> oldProps;
    QMap<QString, QString> newProps;

    if (input.contains(QStringLiteral("name"))) {
        const QString name = input.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty() && (clip->clipType() == ClipType::Timeline || clip->clipType() == ClipType::Text)) {
            return err(QStringLiteral("Timeline/title clip names must not be empty."));
        }
        oldProps.insert(QStringLiteral("kdenlive:clipname"), clip->getProducerProperty(QStringLiteral("kdenlive:clipname")));
        newProps.insert(QStringLiteral("kdenlive:clipname"), name);
    }

    if (input.contains(QStringLiteral("description"))) {
        const QString description = input.value(QStringLiteral("description")).toString();
        const QString key = clip->clipType() == ClipType::TextTemplate ? QStringLiteral("templatetext") : QStringLiteral("kdenlive:description");
        oldProps.insert(key, clip->getProducerProperty(key));
        newProps.insert(key, description);
    }

    if (input.contains(QStringLiteral("tags"))) {
        const QJsonArray tagsArray = input.value(QStringLiteral("tags")).toArray();
        QStringList tags;
        for (const QJsonValue &value : tagsArray) {
            const QString tag = value.toString().trimmed();
            if (tag.isEmpty()) continue;
            if (tag.contains(QLatin1Char(';'))) return err(QStringLiteral("Tags must not contain ';'."));
            if (!tags.contains(tag)) tags.append(tag);
        }
        oldProps.insert(QStringLiteral("kdenlive:tags"), clip->getProducerProperty(QStringLiteral("kdenlive:tags")));
        newProps.insert(QStringLiteral("kdenlive:tags"), tags.join(QLatin1Char(';')));
    }

    if (input.contains(QStringLiteral("rating_raw_0_10"))) {
        const int rating = input.value(QStringLiteral("rating_raw_0_10")).toInt(-1);
        if (rating < 0 || rating > 10) return err(QStringLiteral("rating_raw_0_10 must be between 0 and 10."));
        oldProps.insert(QStringLiteral("kdenlive:rating"), clip->getProducerProperty(QStringLiteral("kdenlive:rating")));
        newProps.insert(QStringLiteral("kdenlive:rating"), QString::number(rating));
    }

    if (newProps.isEmpty()) return err(QStringLiteral("Provide at least one metadata field to update."));
    if (oldProps == newProps) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("changed"), false},
                           {QStringLiteral("metadata"), metadataFor(binId, clip)}, {QStringLiteral("verified"), true}};
    }

    pCore->bin()->slotEditClipCommand(binId, oldProps, newProps);
    const std::shared_ptr<ProjectClip> live = model->getClipByBinID(binId);
    if (!live) return err(QStringLiteral("Metadata edit completed but the bin clip is no longer available."));

    for (auto it = newProps.cbegin(); it != newProps.cend(); ++it) {
        if (live->getProducerProperty(it.key()) != it.value()) {
            return err(QStringLiteral("Metadata edit did not verify property '%1'.").arg(it.key()));
        }
    }

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("changed"), true},
                       {QStringLiteral("metadata"), metadataFor(binId, live)}, {QStringLiteral("verified"), true}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required},
                       {QStringLiteral("additionalProperties"), false}};
}
} // namespace

bool registerVibeCutBinMetadataTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject getInput = objectSchema(
        QJsonObject{{QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}},
        QJsonArray{QStringLiteral("bin_id")});
    const QJsonObject getSchema{{QStringLiteral("name"), QStringLiteral("bin_metadata_get")},
                                {QStringLiteral("description"), QStringLiteral("Read user-curated project-bin metadata for a clip: name, description, tags and Kdenlive rating value, plus type/usage context.")},
                                {QStringLiteral("input_schema"), getInput}};
    VibeCutToolPolicy getPolicy;
    getPolicy.name = QStringLiteral("bin_metadata_get");
    getPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(getSchema, getPolicy, getMetadata, error)) return false;

    const QJsonObject setInput = objectSchema(
        QJsonObject{{QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                    {QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                    {QStringLiteral("description"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                    {QStringLiteral("tags"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                         {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                    {QStringLiteral("rating_raw_0_10"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                                     {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 10},
                                                                     {QStringLiteral("description"), QStringLiteral("Kdenlive's raw rating scale. Full-star UI ratings commonly use even values 0,2,4,6,8,10.")}}}},
        QJsonArray{QStringLiteral("bin_id")});
    const QJsonObject setSchema{{QStringLiteral("name"), QStringLiteral("bin_metadata_set")},
                                {QStringLiteral("description"), QStringLiteral("Update one or more bin metadata fields in a single Kdenlive EditClipCommand so name/description/tags/rating remain normal undoable project state and can feed later search/editorial intelligence." )},
                                {QStringLiteral("input_schema"), setInput}};
    VibeCutToolPolicy setPolicy;
    setPolicy.name = QStringLiteral("bin_metadata_set");
    setPolicy.risk = VibeCutToolRisk::ReversibleEdit;
    setPolicy.reversible = true;
    setPolicy.mutatesProject = true;
    return surface.registerTool(setSchema, setPolicy, setMetadata, error);
}
