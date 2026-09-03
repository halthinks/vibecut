/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutspeakeridentity.h"

#include "core.h"
#include "doc/kdenlivedoc.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace {
QString pathFor(const QUrl &projectUrl)
{
    if (!projectUrl.isValid() || !projectUrl.isLocalFile() || projectUrl.toLocalFile().isEmpty()) return QString();
    return QFileInfo(projectUrl.toLocalFile()).absoluteDir().filePath(VibeCutSpeakerIdentityStore::fileName());
}

bool saveForProjectUrl(const QUrl &projectUrl, const QJsonObject &root, QString *error)
{
    if (error) error->clear();
    const QString path = pathFor(projectUrl);
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("Project must be saved locally before speaker identities can be persisted.");
        return false;
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (data.size() > VibeCutSpeakerIdentityStore::MaxBytes) {
        if (error) *error = QStringLiteral("Speaker identity data would exceed the %1 byte limit.").arg(VibeCutSpeakerIdentityStore::MaxBytes);
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) *error = QStringLiteral("Could not atomically commit speaker identities to %1.").arg(path);
        return false;
    }
    return true;
}

bool validateRoot(const QJsonObject &root, QString *error)
{
    if (root.value(QStringLiteral("version")).toInt(-1) != VibeCutSpeakerIdentityStore::SchemaVersion) {
        if (error) *error = QStringLiteral("Unsupported speaker identity schema version.");
        return false;
    }
    const QJsonArray entities = root.value(QStringLiteral("entities")).toArray();
    const QJsonArray associations = root.value(QStringLiteral("associations")).toArray();
    if (entities.size() > VibeCutSpeakerIdentityStore::MaxEntities || associations.size() > VibeCutSpeakerIdentityStore::MaxAssociations) {
        if (error) *error = QStringLiteral("Speaker identity data exceeds configured entity/association limits.");
        return false;
    }

    QSet<QString> entityIds;
    for (const QJsonValue &value : entities) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("Speaker identities contain a non-object entity.");
            return false;
        }
        const QJsonObject entity = value.toObject();
        const QString id = entity.value(QStringLiteral("id")).toString().trimmed();
        const QString name = entity.value(QStringLiteral("display_name")).toString().trimmed();
        if (id.isEmpty() || name.isEmpty() || name.size() > 256 || entityIds.contains(id)) {
            if (error) *error = QStringLiteral("Speaker entities require unique ids and non-empty display names up to 256 characters.");
            return false;
        }
        entityIds.insert(id);
    }

    QSet<QString> associationKeys;
    for (const QJsonValue &value : associations) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("Speaker identities contain a non-object association.");
            return false;
        }
        const QJsonObject association = value.toObject();
        VibeCutSpeakerClusterKey key;
        key.sourceId = association.value(QStringLiteral("source_id")).toString().trimmed();
        key.sourceFingerprint = association.value(QStringLiteral("source_fingerprint")).toString().trimmed();
        key.extractorId = association.value(QStringLiteral("extractor_id")).toString().trimmed();
        key.extractorVersion = association.value(QStringLiteral("extractor_version")).toString().trimmed();
        key.speakerClusterId = association.value(QStringLiteral("speaker_cluster_id")).toString().trimmed();
        const QString entityId = association.value(QStringLiteral("entity_id")).toString().trimmed();
        const QString stable = key.stableKey();
        if (!key.valid() || entityId.isEmpty() || !entityIds.contains(entityId) || associationKeys.contains(stable)) {
            if (error) *error = QStringLiteral("Speaker associations require a unique valid cluster key and an existing entity id.");
            return false;
        }
        associationKeys.insert(stable);
    }
    return true;
}

QUrl currentProjectUrl()
{
    return pCore && pCore->currentDoc() ? pCore->currentDoc()->url() : QUrl();
}
}

bool VibeCutSpeakerClusterKey::valid() const
{
    return !sourceId.trimmed().isEmpty() && !sourceFingerprint.trimmed().isEmpty() && !extractorId.trimmed().isEmpty() &&
           !extractorVersion.trimmed().isEmpty() && !speakerClusterId.trimmed().isEmpty() && speakerClusterId.trimmed().size() <= 128;
}

QString VibeCutSpeakerClusterKey::stableKey() const
{
    if (!valid()) return QString();
    const QByteArray payload = sourceId.trimmed().toUtf8() + '\n' + sourceFingerprint.trimmed().toUtf8() + '\n' +
                               extractorId.trimmed().toUtf8() + '\n' + extractorVersion.trimmed().toUtf8() + '\n' +
                               speakerClusterId.trimmed().toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

QJsonObject VibeCutSpeakerClusterKey::toJson() const
{
    return QJsonObject{{QStringLiteral("source_id"), sourceId.trimmed()},
                       {QStringLiteral("source_fingerprint"), sourceFingerprint.trimmed()},
                       {QStringLiteral("extractor_id"), extractorId.trimmed()},
                       {QStringLiteral("extractor_version"), extractorVersion.trimmed()},
                       {QStringLiteral("speaker_cluster_id"), speakerClusterId.trimmed()},
                       {QStringLiteral("cluster_key"), stableKey()}};
}

QString VibeCutSpeakerIdentityStore::fileName()
{
    return QStringLiteral(".vibecutspeakers.json");
}

QJsonObject VibeCutSpeakerIdentityStore::emptyRoot()
{
    return QJsonObject{{QStringLiteral("version"), SchemaVersion},
                       {QStringLiteral("entities"), QJsonArray()},
                       {QStringLiteral("associations"), QJsonArray()}};
}

QJsonObject VibeCutSpeakerIdentityStore::loadForProjectUrl(const QUrl &projectUrl, QString *error)
{
    if (error) error->clear();
    const QString path = pathFor(projectUrl);
    if (path.isEmpty()) return emptyRoot();
    QFile file(path);
    if (!file.exists()) return emptyRoot();
    if (file.size() > MaxBytes) {
        if (error) *error = QStringLiteral("%1 exceeds the %2 byte speaker-identity limit.").arg(path).arg(MaxBytes);
        return QJsonObject();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Could not read %1: %2").arg(path, file.errorString());
        return QJsonObject();
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Speaker identity JSON is malformed: %1").arg(parseError.errorString());
        return QJsonObject();
    }
    const QJsonObject root = document.object();
    QString validationError;
    if (!validateRoot(root, &validationError)) {
        if (error) *error = validationError;
        return QJsonObject();
    }
    return root;
}

QJsonObject VibeCutSpeakerIdentityStore::loadCurrent(QString *error)
{
    const QUrl url = currentProjectUrl();
    if (!url.isValid() || !url.isLocalFile()) {
        if (error) *error = QStringLiteral("Current project must be saved locally before speaker identities can be used.");
        return QJsonObject();
    }
    return loadForProjectUrl(url, error);
}

bool VibeCutSpeakerIdentityStore::upsertEntityForProjectUrl(const QUrl &projectUrl,
                                                            const QString &entityId,
                                                            const QString &displayName,
                                                            QString *resolvedEntityId,
                                                            QString *error)
{
    if (error) error->clear();
    const QString name = displayName.trimmed();
    if (name.isEmpty() || name.size() > 256) {
        if (error) *error = QStringLiteral("Speaker display_name must contain 1 to 256 characters.");
        return false;
    }
    QString loadError;
    QJsonObject root = loadForProjectUrl(projectUrl, &loadError);
    if (!loadError.isEmpty()) {
        if (error) *error = loadError;
        return false;
    }
    QJsonArray entities = root.value(QStringLiteral("entities")).toArray();
    const QString id = entityId.trimmed().isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : entityId.trimmed();
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    bool found = false;
    for (int i = 0; i < entities.size(); ++i) {
        QJsonObject entity = entities.at(i).toObject();
        if (entity.value(QStringLiteral("id")).toString() != id) continue;
        entity.insert(QStringLiteral("display_name"), name);
        entity.insert(QStringLiteral("updated_utc"), now);
        entities[i] = entity;
        found = true;
        break;
    }
    if (!found) {
        if (entities.size() >= MaxEntities) {
            if (error) *error = QStringLiteral("Speaker entity limit reached.");
            return false;
        }
        entities.append(QJsonObject{{QStringLiteral("id"), id},
                                    {QStringLiteral("display_name"), name},
                                    {QStringLiteral("created_utc"), now},
                                    {QStringLiteral("updated_utc"), now}});
    }
    root.insert(QStringLiteral("entities"), entities);
    if (!saveForProjectUrl(projectUrl, root, error)) return false;
    if (resolvedEntityId) *resolvedEntityId = id;
    return true;
}

bool VibeCutSpeakerIdentityStore::upsertEntityCurrent(const QString &entityId,
                                                       const QString &displayName,
                                                       QString *resolvedEntityId,
                                                       QString *error)
{
    return upsertEntityForProjectUrl(currentProjectUrl(), entityId, displayName, resolvedEntityId, error);
}

bool VibeCutSpeakerIdentityStore::assignClusterForProjectUrl(const QUrl &projectUrl,
                                                             const VibeCutSpeakerClusterKey &cluster,
                                                             const QString &entityId,
                                                             QString *error)
{
    if (error) error->clear();
    if (!cluster.valid()) {
        if (error) *error = QStringLiteral("Speaker cluster association key is incomplete or invalid.");
        return false;
    }
    const QString targetEntityId = entityId.trimmed();
    if (targetEntityId.isEmpty()) {
        if (error) *error = QStringLiteral("Speaker cluster assignment requires entity_id.");
        return false;
    }
    QString loadError;
    QJsonObject root = loadForProjectUrl(projectUrl, &loadError);
    if (!loadError.isEmpty()) {
        if (error) *error = loadError;
        return false;
    }
    const QJsonArray entities = root.value(QStringLiteral("entities")).toArray();
    bool entityExists = false;
    for (const QJsonValue &value : entities) {
        if (value.toObject().value(QStringLiteral("id")).toString() == targetEntityId) {
            entityExists = true;
            break;
        }
    }
    if (!entityExists) {
        if (error) *error = QStringLiteral("Unknown speaker entity_id: %1").arg(targetEntityId);
        return false;
    }

    QJsonArray associations = root.value(QStringLiteral("associations")).toArray();
    const QString stable = cluster.stableKey();
    QJsonArray next;
    for (const QJsonValue &value : associations) {
        const QJsonObject association = value.toObject();
        if (association.value(QStringLiteral("cluster_key")).toString() == stable) continue;
        next.append(association);
    }
    if (next.size() >= MaxAssociations) {
        if (error) *error = QStringLiteral("Speaker association limit reached.");
        return false;
    }
    QJsonObject association = cluster.toJson();
    association.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    association.insert(QStringLiteral("entity_id"), targetEntityId);
    association.insert(QStringLiteral("assigned_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    next.append(association);
    root.insert(QStringLiteral("associations"), next);
    return saveForProjectUrl(projectUrl, root, error);
}

bool VibeCutSpeakerIdentityStore::assignClusterCurrent(const VibeCutSpeakerClusterKey &cluster,
                                                        const QString &entityId,
                                                        QString *error)
{
    return assignClusterForProjectUrl(currentProjectUrl(), cluster, entityId, error);
}

bool VibeCutSpeakerIdentityStore::unassignClusterForProjectUrl(const QUrl &projectUrl,
                                                               const VibeCutSpeakerClusterKey &cluster,
                                                               QString *error)
{
    if (error) error->clear();
    if (!cluster.valid()) {
        if (error) *error = QStringLiteral("Speaker cluster association key is incomplete or invalid.");
        return false;
    }
    QString loadError;
    QJsonObject root = loadForProjectUrl(projectUrl, &loadError);
    if (!loadError.isEmpty()) {
        if (error) *error = loadError;
        return false;
    }
    const QString stable = cluster.stableKey();
    const QJsonArray associations = root.value(QStringLiteral("associations")).toArray();
    QJsonArray next;
    bool removed = false;
    for (const QJsonValue &value : associations) {
        const QJsonObject association = value.toObject();
        if (association.value(QStringLiteral("cluster_key")).toString() == stable) {
            removed = true;
            continue;
        }
        next.append(association);
    }
    if (!removed) {
        if (error) *error = QStringLiteral("Speaker cluster is not currently assigned.");
        return false;
    }
    root.insert(QStringLiteral("associations"), next);
    return saveForProjectUrl(projectUrl, root, error);
}

bool VibeCutSpeakerIdentityStore::unassignClusterCurrent(const VibeCutSpeakerClusterKey &cluster,
                                                          QString *error)
{
    return unassignClusterForProjectUrl(currentProjectUrl(), cluster, error);
}

QJsonObject VibeCutSpeakerIdentityStore::resolve(const QJsonObject &root, const VibeCutSpeakerClusterKey &cluster)
{
    if (!cluster.valid()) return QJsonObject();
    const QString stable = cluster.stableKey();
    QString entityId;
    for (const QJsonValue &value : root.value(QStringLiteral("associations")).toArray()) {
        const QJsonObject association = value.toObject();
        if (association.value(QStringLiteral("cluster_key")).toString() == stable) {
            entityId = association.value(QStringLiteral("entity_id")).toString();
            break;
        }
    }
    if (entityId.isEmpty()) return QJsonObject();
    for (const QJsonValue &value : root.value(QStringLiteral("entities")).toArray()) {
        const QJsonObject entity = value.toObject();
        if (entity.value(QStringLiteral("id")).toString() == entityId) return entity;
    }
    return QJsonObject();
}
