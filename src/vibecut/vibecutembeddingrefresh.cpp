/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutembeddingstore.h"

#include "core.h"
#include "doc/kdenlivedoc.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace {
QString pathFor(const QUrl &projectUrl)
{
    if (!projectUrl.isValid() || !projectUrl.isLocalFile() || projectUrl.toLocalFile().isEmpty()) return QString();
    return QFileInfo(projectUrl.toLocalFile()).absoluteDir().filePath(VibeCutEmbeddingStore::fileName());
}

QUrl currentProjectUrl()
{
    return pCore && pCore->currentDoc() ? pCore->currentDoc()->url() : QUrl();
}

QString recordKey(const VibeCutEmbeddingRecord &record)
{
    return record.anchorKind + QLatin1Char('\n') + record.anchorId + QLatin1Char('\n') +
           record.sourceId + QLatin1Char('\n') + record.sourceFingerprint + QLatin1Char('\n') + record.modality;
}

bool writeValidatedRoot(const QUrl &projectUrl, const QJsonObject &root, QString *error)
{
    if (error) error->clear();
    const QString path = pathFor(projectUrl);
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("Project must be saved locally before embeddings can be persisted.");
        return false;
    }
    // Re-read every proposed record through the authoritative record parser so
    // this bulk path cannot bypass vector/provenance/unit-normalization rules.
    const QJsonArray raw = root.value(QStringLiteral("records")).toArray();
    if (root.value(QStringLiteral("version")).toInt(-1) != VibeCutEmbeddingStore::SchemaVersion ||
        raw.size() > VibeCutEmbeddingStore::MaxRecords) {
        if (error) *error = QStringLiteral("Embedding refresh root is malformed or exceeds the record limit.");
        return false;
    }
    QJsonArray normalized;
    for (const QJsonValue &value : raw) {
        VibeCutEmbeddingRecord record;
        QString recordError;
        if (!value.isObject() || !VibeCutEmbeddingRecord::fromJson(value.toObject(), record, &recordError)) {
            if (error) *error = QStringLiteral("Embedding refresh contains an invalid record: %1").arg(recordError);
            return false;
        }
        normalized.append(record.toJson());
    }
    const QJsonObject checked{{QStringLiteral("version"), VibeCutEmbeddingStore::SchemaVersion},
                              {QStringLiteral("records"), normalized}};
    const QByteArray data = QJsonDocument(checked).toJson(QJsonDocument::Compact);
    if (data.size() > VibeCutEmbeddingStore::MaxBytes) {
        if (error) *error = QStringLiteral("Embedding refresh would exceed the %1 byte limit.").arg(VibeCutEmbeddingStore::MaxBytes);
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) *error = QStringLiteral("Could not atomically commit refreshed embeddings to %1.").arg(path);
        return false;
    }
    return true;
}
} // namespace

bool VibeCutEmbeddingStore::replaceProducerModelForProjectUrl(const QUrl &projectUrl,
                                                              const QString &producerId,
                                                              const QString &producerVersion,
                                                              const QString &model,
                                                              const QString &modelRevision,
                                                              const QList<VibeCutEmbeddingRecord> &records,
                                                              QString *error)
{
    if (error) error->clear();
    const QString wantedProducer = producerId.trimmed();
    const QString wantedProducerVersion = producerVersion.trimmed();
    const QString wantedModel = model.trimmed();
    const QString wantedRevision = modelRevision.trimmed();
    if (wantedProducer.isEmpty() || wantedProducerVersion.isEmpty() || wantedModel.isEmpty() || wantedRevision.isEmpty()) {
        if (error) *error = QStringLiteral("Embedding refresh requires non-empty producer/model identity fields.");
        return false;
    }
    if (records.size() > MaxRecords) {
        if (error) *error = QStringLiteral("Embedding refresh exceeds the %1 record limit.").arg(MaxRecords);
        return false;
    }

    QList<VibeCutEmbeddingRecord> normalizedIncoming;
    normalizedIncoming.reserve(records.size());
    QSet<QString> keys;
    int expectedDimension = -1;
    for (const VibeCutEmbeddingRecord &input : records) {
        if (input.producerId.trimmed() != wantedProducer || input.producerVersion.trimmed() != wantedProducerVersion ||
            input.model.trimmed() != wantedModel || input.modelRevision.trimmed() != wantedRevision) {
            if (error) *error = QStringLiteral("Every refreshed embedding must match the declared producer id/version and model/revision.");
            return false;
        }
        VibeCutEmbeddingRecord record;
        QString recordError;
        if (!VibeCutEmbeddingRecord::fromJson(input.toJson(), record, &recordError)) {
            if (error) *error = QStringLiteral("Embedding refresh record was rejected: %1").arg(recordError);
            return false;
        }
        if (expectedDimension < 0) expectedDimension = record.vector.size();
        if (record.vector.size() != expectedDimension) {
            if (error) *error = QStringLiteral("One producer/model refresh may not mix embedding dimensions.");
            return false;
        }
        const QString key = recordKey(record);
        if (keys.contains(key)) {
            if (error) *error = QStringLiteral("Embedding refresh contains duplicate anchor/source/modality records.");
            return false;
        }
        keys.insert(key);
        normalizedIncoming.append(record);
    }

    QString loadError;
    const QJsonObject current = loadForProjectUrl(projectUrl, &loadError);
    if (!loadError.isEmpty()) {
        if (error) *error = loadError;
        return false;
    }
    QJsonArray next;
    for (const QJsonValue &value : current.value(QStringLiteral("records")).toArray()) {
        VibeCutEmbeddingRecord existing;
        QString existingError;
        if (!VibeCutEmbeddingRecord::fromJson(value.toObject(), existing, &existingError)) {
            if (error) *error = existingError;
            return false;
        }
        // Producer version is deliberately not part of the removal predicate:
        // a refreshed producer supersedes its older implementation versions for
        // this exact embedding space rather than leaving stale vectors searchable.
        if (existing.producerId == wantedProducer && existing.model == wantedModel &&
            existing.modelRevision == wantedRevision) {
            continue;
        }
        next.append(existing.toJson());
    }
    if (next.size() + normalizedIncoming.size() > MaxRecords) {
        if (error) *error = QStringLiteral("Embedding refresh would exceed the %1 record limit.").arg(MaxRecords);
        return false;
    }
    for (const VibeCutEmbeddingRecord &record : normalizedIncoming) next.append(record.toJson());
    return writeValidatedRoot(projectUrl,
                              QJsonObject{{QStringLiteral("version"), SchemaVersion},
                                          {QStringLiteral("records"), next}},
                              error);
}

bool VibeCutEmbeddingStore::replaceProducerModelCurrent(const QString &producerId,
                                                        const QString &producerVersion,
                                                        const QString &model,
                                                        const QString &modelRevision,
                                                        const QList<VibeCutEmbeddingRecord> &records,
                                                        QString *error)
{
    return replaceProducerModelForProjectUrl(currentProjectUrl(), producerId, producerVersion,
                                             model, modelRevision, records, error);
}
