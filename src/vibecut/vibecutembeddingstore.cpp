/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutembeddingstore.h"

#include "core.h"
#include "doc/kdenlivedoc.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace {
QString pathFor(const QUrl &projectUrl)
{
    if (!projectUrl.isValid() || !projectUrl.isLocalFile() || projectUrl.toLocalFile().isEmpty()) return QString();
    return QFileInfo(projectUrl.toLocalFile()).absoluteDir().filePath(VibeCutEmbeddingStore::fileName());
}

bool finiteUnitVector(const QVector<double> &vector, QString *error)
{
    if (vector.isEmpty() || vector.size() > VibeCutEmbeddingStore::MaxDimensions) {
        if (error) *error = QStringLiteral("Embedding dimension must be between 1 and %1.").arg(VibeCutEmbeddingStore::MaxDimensions);
        return false;
    }
    long double normSquared = 0.0L;
    for (double value : vector) {
        if (!std::isfinite(value)) {
            if (error) *error = QStringLiteral("Embedding vector contains a non-finite value.");
            return false;
        }
        normSquared += static_cast<long double>(value) * static_cast<long double>(value);
    }
    const double norm = std::sqrt(static_cast<double>(normSquared));
    if (!std::isfinite(norm) || std::abs(norm - 1.0) > 0.001) {
        if (error) *error = QStringLiteral("Persisted embedding vectors must be unit-normalized within tolerance 0.001.");
        return false;
    }
    return true;
}

bool validateRecord(const VibeCutEmbeddingRecord &record, QString *error)
{
    if (error) error->clear();
    if (record.anchorKind.trimmed().isEmpty() || record.anchorKind.trimmed().size() > 64 ||
        record.anchorId.trimmed().isEmpty() || record.anchorId.trimmed().size() > 512) {
        if (error) *error = QStringLiteral("Embedding records require bounded non-empty anchor_kind and anchor_id.");
        return false;
    }
    if (record.sourceId.trimmed().size() > 512 || record.sourceFingerprint.trimmed().size() > 256) {
        if (error) *error = QStringLiteral("Embedding source identity fields exceed configured bounds.");
        return false;
    }
    if (!record.sourceId.trimmed().isEmpty() && record.sourceFingerprint.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Source-backed embeddings require source_fingerprint when source_id is present.");
        return false;
    }
    if (record.modality.trimmed().isEmpty() || record.modality.trimmed().size() > 64 ||
        record.model.trimmed().isEmpty() || record.model.trimmed().size() > 256 ||
        record.modelRevision.trimmed().isEmpty() || record.modelRevision.trimmed().size() > 128 ||
        record.producerId.trimmed().isEmpty() || record.producerId.trimmed().size() > 128 ||
        record.producerVersion.trimmed().isEmpty() || record.producerVersion.trimmed().size() > 128) {
        if (error) *error = QStringLiteral("Embedding records require bounded modality, model/revision and producer provenance.");
        return false;
    }
    if (record.startFrame < -1 || record.endFrame < -1 ||
        (record.startFrame >= 0 && record.endFrame >= 0 && record.endFrame < record.startFrame)) {
        if (error) *error = QStringLiteral("Embedding record has an invalid frame range.");
        return false;
    }
    if ((record.startFrame < 0) != (record.endFrame < 0)) {
        if (error) *error = QStringLiteral("Embedding frame bounds must either both be unknown or both be present.");
        return false;
    }
    return finiteUnitVector(record.vector, error);
}

bool validateRoot(const QJsonObject &root, QString *error)
{
    if (error) error->clear();
    if (root.value(QStringLiteral("version")).toInt(-1) != VibeCutEmbeddingStore::SchemaVersion ||
        !root.value(QStringLiteral("records")).isArray()) {
        if (error) *error = QStringLiteral("Unsupported or malformed embedding-store schema.");
        return false;
    }
    const QJsonArray records = root.value(QStringLiteral("records")).toArray();
    if (records.size() > VibeCutEmbeddingStore::MaxRecords) {
        if (error) *error = QStringLiteral("Embedding store exceeds the %1 record limit.").arg(VibeCutEmbeddingStore::MaxRecords);
        return false;
    }
    for (const QJsonValue &value : records) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("Embedding store contains a non-object record.");
            return false;
        }
        VibeCutEmbeddingRecord record;
        QString recordError;
        if (!VibeCutEmbeddingRecord::fromJson(value.toObject(), record, &recordError)) {
            if (error) *error = QStringLiteral("Invalid embedding record: %1").arg(recordError);
            return false;
        }
    }
    return true;
}

bool saveForProjectUrl(const QUrl &projectUrl, const QJsonObject &root, QString *error)
{
    if (error) error->clear();
    const QString path = pathFor(projectUrl);
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("Project must be saved locally before embeddings can be persisted.");
        return false;
    }
    QString validationError;
    if (!validateRoot(root, &validationError)) {
        if (error) *error = validationError;
        return false;
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (data.size() > VibeCutEmbeddingStore::MaxBytes) {
        if (error) *error = QStringLiteral("Embedding store would exceed the %1 byte limit.").arg(VibeCutEmbeddingStore::MaxBytes);
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) *error = QStringLiteral("Could not atomically commit embeddings to %1.").arg(path);
        return false;
    }
    return true;
}

QUrl currentProjectUrl()
{
    return pCore && pCore->currentDoc() ? pCore->currentDoc()->url() : QUrl();
}

QString sliceKey(const VibeCutEmbeddingRecord &record)
{
    return record.anchorKind.trimmed() + QLatin1Char('\n') + record.anchorId.trimmed() + QLatin1Char('\n') +
           record.sourceId.trimmed() + QLatin1Char('\n') + record.sourceFingerprint.trimmed() + QLatin1Char('\n') +
           record.modality.trimmed() + QLatin1Char('\n') + record.model.trimmed() + QLatin1Char('\n') +
           record.modelRevision.trimmed() + QLatin1Char('\n') + record.producerId.trimmed();
}
} // namespace

QJsonObject VibeCutEmbeddingRecord::toJson() const
{
    QJsonArray values;
    for (double value : vector) values.append(value);
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("anchor_kind"), anchorKind},
                       {QStringLiteral("anchor_id"), anchorId},
                       {QStringLiteral("source_id"), sourceId},
                       {QStringLiteral("source_fingerprint"), sourceFingerprint},
                       {QStringLiteral("modality"), modality},
                       {QStringLiteral("model"), model},
                       {QStringLiteral("model_revision"), modelRevision},
                       {QStringLiteral("producer_id"), producerId},
                       {QStringLiteral("producer_version"), producerVersion},
                       {QStringLiteral("start_frame"), startFrame},
                       {QStringLiteral("end_frame"), endFrame},
                       {QStringLiteral("dimension"), vector.size()},
                       {QStringLiteral("unit_normalized"), true},
                       {QStringLiteral("vector"), values},
                       {QStringLiteral("produced_utc"), producedUtc},
                       {QStringLiteral("metadata"), metadata}};
}

bool VibeCutEmbeddingRecord::fromJson(const QJsonObject &object, VibeCutEmbeddingRecord &record, QString *error)
{
    if (error) error->clear();
    record.id = object.value(QStringLiteral("id")).toString().trimmed();
    record.anchorKind = object.value(QStringLiteral("anchor_kind")).toString().trimmed();
    record.anchorId = object.value(QStringLiteral("anchor_id")).toString().trimmed();
    record.sourceId = object.value(QStringLiteral("source_id")).toString().trimmed();
    record.sourceFingerprint = object.value(QStringLiteral("source_fingerprint")).toString().trimmed();
    record.modality = object.value(QStringLiteral("modality")).toString().trimmed();
    record.model = object.value(QStringLiteral("model")).toString().trimmed();
    record.modelRevision = object.value(QStringLiteral("model_revision")).toString().trimmed();
    record.producerId = object.value(QStringLiteral("producer_id")).toString().trimmed();
    record.producerVersion = object.value(QStringLiteral("producer_version")).toString().trimmed();
    record.startFrame = object.value(QStringLiteral("start_frame")).toInt(-1);
    record.endFrame = object.value(QStringLiteral("end_frame")).toInt(-1);
    record.producedUtc = object.value(QStringLiteral("produced_utc")).toString().trimmed();
    record.metadata = object.value(QStringLiteral("metadata")).toObject();
    record.vector.clear();
    const QJsonArray values = object.value(QStringLiteral("vector")).toArray();
    if (object.value(QStringLiteral("dimension")).toInt(-1) != values.size() ||
        !object.value(QStringLiteral("unit_normalized")).toBool(false)) {
        if (error) *error = QStringLiteral("Embedding dimension/unit-normalization metadata is missing or inconsistent.");
        return false;
    }
    record.vector.reserve(values.size());
    for (const QJsonValue &value : values) {
        if (!value.isDouble()) {
            if (error) *error = QStringLiteral("Embedding vector contains a non-numeric value.");
            return false;
        }
        record.vector.append(value.toDouble());
    }
    if (!validateRecord(record, error)) return false;
    if (record.id.isEmpty()) record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (record.producedUtc.isEmpty()) record.producedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    return true;
}

QJsonObject VibeCutEmbeddingSearchHit::toJson() const
{
    return QJsonObject{{QStringLiteral("embedding_id"), embeddingId},
                       {QStringLiteral("anchor_kind"), anchorKind},
                       {QStringLiteral("anchor_id"), anchorId},
                       {QStringLiteral("source_id"), sourceId},
                       {QStringLiteral("source_fingerprint"), sourceFingerprint},
                       {QStringLiteral("modality"), modality},
                       {QStringLiteral("start_frame"), startFrame},
                       {QStringLiteral("end_frame"), endFrame},
                       {QStringLiteral("similarity"), similarity},
                       {QStringLiteral("score_semantics"), QStringLiteral("cosine_similarity_same_embedding_space")},
                       {QStringLiteral("metadata"), metadata}};
}

QString VibeCutEmbeddingStore::fileName()
{
    return QStringLiteral(".vibecutembeddings.json");
}

QJsonObject VibeCutEmbeddingStore::emptyRoot()
{
    return QJsonObject{{QStringLiteral("version"), SchemaVersion}, {QStringLiteral("records"), QJsonArray()}};
}

QJsonObject VibeCutEmbeddingStore::loadForProjectUrl(const QUrl &projectUrl, QString *error)
{
    if (error) error->clear();
    const QString path = pathFor(projectUrl);
    if (path.isEmpty()) return emptyRoot();
    QFile file(path);
    if (!file.exists()) return emptyRoot();
    if (file.size() > MaxBytes) {
        if (error) *error = QStringLiteral("%1 exceeds the %2 byte embedding-store limit.").arg(path).arg(MaxBytes);
        return QJsonObject();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Could not read %1: %2").arg(path, file.errorString());
        return QJsonObject();
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Embedding store JSON is malformed: %1").arg(parseError.errorString());
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

QJsonObject VibeCutEmbeddingStore::loadCurrent(QString *error)
{
    const QUrl url = currentProjectUrl();
    if (!url.isValid() || !url.isLocalFile()) {
        if (error) *error = QStringLiteral("Current project must be saved locally before embeddings can be used.");
        return QJsonObject();
    }
    return loadForProjectUrl(url, error);
}

bool VibeCutEmbeddingStore::upsertForProjectUrl(const QUrl &projectUrl,
                                                const VibeCutEmbeddingRecord &input,
                                                QString *error)
{
    if (error) error->clear();
    VibeCutEmbeddingRecord record = input;
    QString validationError;
    if (!validateRecord(record, &validationError)) {
        if (error) *error = validationError;
        return false;
    }
    if (record.id.trimmed().isEmpty()) record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (record.producedUtc.trimmed().isEmpty()) record.producedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    QString loadError;
    QJsonObject root = loadForProjectUrl(projectUrl, &loadError);
    if (!loadError.isEmpty()) {
        if (error) *error = loadError;
        return false;
    }
    const QString targetKey = sliceKey(record);
    QJsonArray next;
    for (const QJsonValue &value : root.value(QStringLiteral("records")).toArray()) {
        VibeCutEmbeddingRecord existing;
        QString existingError;
        if (!VibeCutEmbeddingRecord::fromJson(value.toObject(), existing, &existingError)) {
            if (error) *error = existingError;
            return false;
        }
        if (sliceKey(existing) == targetKey) continue;
        next.append(existing.toJson());
    }
    if (next.size() >= MaxRecords) {
        if (error) *error = QStringLiteral("Embedding record limit reached.");
        return false;
    }
    next.append(record.toJson());
    root.insert(QStringLiteral("records"), next);
    return saveForProjectUrl(projectUrl, root, error);
}

bool VibeCutEmbeddingStore::upsertCurrent(const VibeCutEmbeddingRecord &record, QString *error)
{
    return upsertForProjectUrl(currentProjectUrl(), record, error);
}

bool VibeCutEmbeddingStore::normalizeVector(const QVector<double> &input, QVector<double> &unit, QString *error)
{
    if (error) error->clear();
    unit.clear();
    if (input.isEmpty() || input.size() > MaxDimensions) {
        if (error) *error = QStringLiteral("Embedding vector dimension must be between 1 and %1.").arg(MaxDimensions);
        return false;
    }
    long double normSquared = 0.0L;
    for (double value : input) {
        if (!std::isfinite(value)) {
            if (error) *error = QStringLiteral("Embedding vector contains a non-finite value.");
            return false;
        }
        normSquared += static_cast<long double>(value) * static_cast<long double>(value);
    }
    const double norm = std::sqrt(static_cast<double>(normSquared));
    if (!std::isfinite(norm) || norm <= 1e-12) {
        if (error) *error = QStringLiteral("Embedding vector norm must be finite and non-zero.");
        return false;
    }
    unit.reserve(input.size());
    for (double value : input) unit.append(value / norm);
    return true;
}

QList<VibeCutEmbeddingSearchHit> VibeCutEmbeddingStore::cosineSearch(const QJsonObject &root,
                                                                     const QVector<double> &unitQuery,
                                                                     const QString &model,
                                                                     const QString &modelRevision,
                                                                     const QStringList &modalities,
                                                                     int limit,
                                                                     double minSimilarity,
                                                                     QString *error)
{
    if (error) error->clear();
    QString queryError;
    if (!finiteUnitVector(unitQuery, &queryError)) {
        if (error) *error = QStringLiteral("Semantic query vector is invalid: %1").arg(queryError);
        return {};
    }
    const QString wantedModel = model.trimmed();
    const QString wantedRevision = modelRevision.trimmed();
    if (wantedModel.isEmpty() || wantedRevision.isEmpty()) {
        if (error) *error = QStringLiteral("Semantic search requires an exact embedding model and revision.");
        return {};
    }
    QString rootError;
    if (!validateRoot(root, &rootError)) {
        if (error) *error = rootError;
        return {};
    }
    QStringList normalizedModalities;
    for (const QString &modality : modalities) {
        const QString clean = modality.trimmed().toLower();
        if (!clean.isEmpty() && !normalizedModalities.contains(clean)) normalizedModalities.append(clean);
    }
    minSimilarity = qBound(-1.0, minSimilarity, 1.0);
    limit = qBound(1, limit, 100);

    QList<VibeCutEmbeddingSearchHit> hits;
    for (const QJsonValue &value : root.value(QStringLiteral("records")).toArray()) {
        VibeCutEmbeddingRecord record;
        QString recordError;
        if (!VibeCutEmbeddingRecord::fromJson(value.toObject(), record, &recordError)) {
            if (error) *error = recordError;
            return {};
        }
        if (record.model != wantedModel || record.modelRevision != wantedRevision || record.vector.size() != unitQuery.size()) continue;
        if (!normalizedModalities.isEmpty() && !normalizedModalities.contains(record.modality.toLower())) continue;
        long double dot = 0.0L;
        for (int i = 0; i < unitQuery.size(); ++i) dot += static_cast<long double>(unitQuery.at(i)) * record.vector.at(i);
        const double similarity = qBound(-1.0, static_cast<double>(dot), 1.0);
        if (similarity < minSimilarity) continue;
        VibeCutEmbeddingSearchHit hit;
        hit.embeddingId = record.id;
        hit.anchorKind = record.anchorKind;
        hit.anchorId = record.anchorId;
        hit.sourceId = record.sourceId;
        hit.sourceFingerprint = record.sourceFingerprint;
        hit.modality = record.modality;
        hit.startFrame = record.startFrame;
        hit.endFrame = record.endFrame;
        hit.similarity = similarity;
        hit.metadata = record.metadata;
        hit.metadata.insert(QStringLiteral("model"), record.model);
        hit.metadata.insert(QStringLiteral("model_revision"), record.modelRevision);
        hit.metadata.insert(QStringLiteral("producer_id"), record.producerId);
        hit.metadata.insert(QStringLiteral("producer_version"), record.producerVersion);
        hits.append(hit);
    }
    std::sort(hits.begin(), hits.end(), [](const VibeCutEmbeddingSearchHit &a, const VibeCutEmbeddingSearchHit &b) {
        if (a.similarity != b.similarity) return a.similarity > b.similarity;
        if (a.startFrame != b.startFrame) return a.startFrame < b.startFrame;
        if (a.anchorKind != b.anchorKind) return a.anchorKind < b.anchorKind;
        return a.anchorId < b.anchorId;
    });
    while (hits.size() > limit) hits.removeLast();
    return hits;
}
