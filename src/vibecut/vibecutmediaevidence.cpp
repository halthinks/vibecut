/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutmediaevidence.h"

#include "core.h"
#include "doc/kdenlivedoc.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

#include <cmath>

namespace {
QString pathFor(const QUrl &projectUrl)
{
    if (!projectUrl.isValid() || !projectUrl.isLocalFile() || projectUrl.toLocalFile().isEmpty()) return QString();
    return QFileInfo(projectUrl.toLocalFile()).absoluteDir().filePath(VibeCutMediaEvidence::fileName());
}

QJsonObject rootFor(const QJsonArray &records)
{
    return QJsonObject{{QStringLiteral("version"), VibeCutMediaEvidence::SchemaVersion}, {QStringLiteral("records"), records}};
}

bool saveForProjectUrl(const QUrl &projectUrl, const QJsonArray &records, QString *error)
{
    if (error) error->clear();
    if (records.size() > VibeCutMediaEvidence::MaxRecords) {
        if (error) *error = QStringLiteral("Media evidence would exceed the %1 record limit.").arg(VibeCutMediaEvidence::MaxRecords);
        return false;
    }
    const QString path = pathFor(projectUrl);
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("Project must be saved locally before media evidence can be persisted.");
        return false;
    }
    const QByteArray data = QJsonDocument(rootFor(records)).toJson(QJsonDocument::Compact);
    if (data.size() > VibeCutMediaEvidence::MaxBytes) {
        if (error) *error = QStringLiteral("Media evidence would exceed the %1 byte limit.").arg(VibeCutMediaEvidence::MaxBytes);
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) *error = QStringLiteral("Could not atomically commit media evidence to %1.").arg(path);
        return false;
    }
    return true;
}

bool bounded(const QString &value, int maximum)
{
    return value.size() <= maximum;
}
} // namespace

QJsonObject VibeCutMediaEvidenceRecord::toJson() const
{
    return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("source_id"), sourceId},
                       {QStringLiteral("source_fingerprint"), sourceFingerprint}, {QStringLiteral("extractor_id"), extractorId},
                       {QStringLiteral("extractor_version"), extractorVersion}, {QStringLiteral("kind"), kind},
                       {QStringLiteral("start_frame"), startFrame}, {QStringLiteral("end_frame"), endFrame},
                       {QStringLiteral("text"), text}, {QStringLiteral("confidence"), confidence},
                       {QStringLiteral("produced_utc"), producedUtc}, {QStringLiteral("metadata"), metadata}};
}

bool VibeCutMediaEvidenceRecord::fromJson(const QJsonObject &object, VibeCutMediaEvidenceRecord &record, QString *error)
{
    if (error) error->clear();
    if (object.contains(QStringLiteral("confidence")) && !object.value(QStringLiteral("confidence")).isDouble()) {
        if (error) *error = QStringLiteral("Media evidence confidence must be numeric.");
        return false;
    }
    if (object.contains(QStringLiteral("metadata")) && !object.value(QStringLiteral("metadata")).isObject()) {
        if (error) *error = QStringLiteral("Media evidence metadata must be an object.");
        return false;
    }

    record.id = object.value(QStringLiteral("id")).toString().trimmed();
    record.sourceId = object.value(QStringLiteral("source_id")).toString().trimmed();
    record.sourceFingerprint = object.value(QStringLiteral("source_fingerprint")).toString().trimmed();
    record.extractorId = object.value(QStringLiteral("extractor_id")).toString().trimmed();
    record.extractorVersion = object.value(QStringLiteral("extractor_version")).toString().trimmed();
    record.kind = object.value(QStringLiteral("kind")).toString().trimmed();
    record.startFrame = object.value(QStringLiteral("start_frame")).toInt(-1);
    record.endFrame = object.value(QStringLiteral("end_frame")).toInt(-1);
    record.text = object.value(QStringLiteral("text")).toString();
    record.confidence = object.value(QStringLiteral("confidence")).toDouble(-1.0);
    record.producedUtc = object.value(QStringLiteral("produced_utc")).toString().trimmed();
    record.metadata = object.value(QStringLiteral("metadata")).toObject();

    if (record.sourceId.isEmpty() || record.sourceFingerprint.isEmpty() || record.extractorId.isEmpty() || record.extractorVersion.isEmpty() || record.kind.isEmpty()) {
        if (error) *error = QStringLiteral("Media evidence requires source_id, source_fingerprint, extractor_id, extractor_version and kind.");
        return false;
    }
    if ((!record.id.isEmpty() && !bounded(record.id, 1024)) || !bounded(record.sourceId, 4096) ||
        !bounded(record.sourceFingerprint, 4096) || !bounded(record.extractorId, 1024) ||
        !bounded(record.extractorVersion, 1024) || !bounded(record.kind, 1024) ||
        !bounded(record.text, 1048576) || (!record.producedUtc.isEmpty() && !bounded(record.producedUtc, 128))) {
        if (error) *error = QStringLiteral("Media evidence exceeds one or more public schema field bounds.");
        return false;
    }
    if (record.startFrame < -1 || record.endFrame < -1 || (record.startFrame >= 0 && record.endFrame >= 0 && record.endFrame < record.startFrame)) {
        if (error) *error = QStringLiteral("Media evidence has an invalid frame range.");
        return false;
    }
    if (!std::isfinite(record.confidence) || !(record.confidence == -1.0 || (record.confidence >= 0.0 && record.confidence <= 1.0))) {
        if (error) *error = QStringLiteral("Media evidence confidence must be -1 (unknown) or between 0 and 1.");
        return false;
    }
    if (record.id.isEmpty()) record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (record.producedUtc.isEmpty()) record.producedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    return true;
}

QString VibeCutMediaEvidence::fileName() { return QStringLiteral(".vibecutmedia.json"); }

bool VibeCutMediaEvidence::canPersistCurrent(QString *error)
{
    if (error) error->clear();
    if (!pCore || !pCore->currentDoc()) {
        if (error) *error = QStringLiteral("No current project is available.");
        return false;
    }
    const QUrl projectUrl = pCore->currentDoc()->url();
    const QString path = pathFor(projectUrl);
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("Project must be saved locally before media analysis can persist evidence.");
        return false;
    }
    const QFileInfo projectInfo(projectUrl.toLocalFile());
    const QDir directory = projectInfo.absoluteDir();
    const QFileInfo directoryInfo(directory.absolutePath());
    if (!directory.exists() || !directoryInfo.isWritable()) {
        if (error) *error = QStringLiteral("Project directory is not writable for media-evidence persistence.");
        return false;
    }
    return true;
}

QJsonArray VibeCutMediaEvidence::loadForProjectUrl(const QUrl &projectUrl, QString *error)
{
    if (error) error->clear();
    const QString path = pathFor(projectUrl);
    if (path.isEmpty()) return QJsonArray();
    QFile file(path);
    if (!file.exists()) return QJsonArray();
    if (file.size() > MaxBytes) {
        if (error) *error = QStringLiteral("%1 exceeds the %2 byte media-evidence limit.").arg(path).arg(MaxBytes);
        return QJsonArray();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Could not read %1: %2").arg(path, file.errorString());
        return QJsonArray();
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Media evidence is malformed: %1").arg(parseError.errorString());
        return QJsonArray();
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt(-1) != SchemaVersion || !root.value(QStringLiteral("records")).isArray()) {
        if (error) *error = QStringLiteral("Unsupported or malformed media-evidence schema.");
        return QJsonArray();
    }
    const QJsonArray raw = root.value(QStringLiteral("records")).toArray();
    if (raw.size() > MaxRecords) {
        if (error) *error = QStringLiteral("Media evidence exceeds the %1 record limit.").arg(MaxRecords);
        return QJsonArray();
    }
    QJsonArray validated;
    for (const QJsonValue &value : raw) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("Media evidence contains a non-object record.");
            return QJsonArray();
        }
        VibeCutMediaEvidenceRecord record;
        QString recordError;
        if (!VibeCutMediaEvidenceRecord::fromJson(value.toObject(), record, &recordError)) {
            if (error) *error = QStringLiteral("Invalid media evidence record: %1").arg(recordError);
            return QJsonArray();
        }
        validated.append(record.toJson());
    }
    return validated;
}

QJsonArray VibeCutMediaEvidence::loadCurrent(QString *error)
{
    if (!pCore || !pCore->currentDoc()) {
        if (error) *error = QStringLiteral("No current project is available.");
        return QJsonArray();
    }
    return loadForProjectUrl(pCore->currentDoc()->url(), error);
}

bool VibeCutMediaEvidence::replaceSourceExtractorCurrent(const QString &sourceId, const QString &sourceFingerprint, const QString &extractorId,
                                                         const QString &extractorVersion, const QList<VibeCutMediaEvidenceRecord> &records, QString *error)
{
    if (error) error->clear();
    if (!pCore || !pCore->currentDoc()) {
        if (error) *error = QStringLiteral("No current project is available.");
        return false;
    }
    if (sourceId.trimmed().isEmpty() || sourceFingerprint.trimmed().isEmpty() || extractorId.trimmed().isEmpty() || extractorVersion.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("source/extractor identity fields must not be empty.");
        return false;
    }
    QString loadError;
    const QJsonArray current = loadCurrent(&loadError);
    if (!loadError.isEmpty()) {
        if (error) *error = loadError;
        return false;
    }
    QJsonArray next;
    for (const QJsonValue &value : current) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("source_id")).toString() == sourceId && object.value(QStringLiteral("extractor_id")).toString() == extractorId) continue;
        next.append(object);
    }
    for (VibeCutMediaEvidenceRecord record : records) {
        if (record.sourceId.isEmpty()) record.sourceId = sourceId;
        if (record.sourceFingerprint.isEmpty()) record.sourceFingerprint = sourceFingerprint;
        if (record.extractorId.isEmpty()) record.extractorId = extractorId;
        if (record.extractorVersion.isEmpty()) record.extractorVersion = extractorVersion;
        if (record.sourceId != sourceId || record.sourceFingerprint != sourceFingerprint || record.extractorId != extractorId || record.extractorVersion != extractorVersion) {
            if (error) *error = QStringLiteral("Replacement records must all match the supplied source/extractor identity.");
            return false;
        }
        QString validationError;
        VibeCutMediaEvidenceRecord normalized;
        if (!VibeCutMediaEvidenceRecord::fromJson(record.toJson(), normalized, &validationError)) {
            if (error) *error = validationError;
            return false;
        }
        next.append(normalized.toJson());
    }
    return saveForProjectUrl(pCore->currentDoc()->url(), next, error);
}

bool VibeCutMediaEvidence::clearCurrent(QString *error)
{
    if (!pCore || !pCore->currentDoc()) {
        if (error) *error = QStringLiteral("No current project is available.");
        return false;
    }
    return saveForProjectUrl(pCore->currentDoc()->url(), QJsonArray(), error);
}
