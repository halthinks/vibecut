/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutsemantictools.h"

#include "vibecutembeddingstore.h"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>

namespace {
const QString kModel = QStringLiteral("sentence-transformers/all-MiniLM-L6-v2");
const QString kModelRevision = QStringLiteral("1110a243fdf4706b3f48f1d95db1a4f5529b4d41");
const QString kProducer = QStringLiteral("semantic_text_minilm");
const QString kProducerVersion = QStringLiteral("1.0.0");

QString textHash(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}
} // namespace

QJsonObject filterVibeCutCurrentSemanticTextEmbeddingRoot(const QJsonObject &root,
                                                          const QList<VibeCutMediaDocument> &documents,
                                                          int *staleSkipped,
                                                          QString *error)
{
    if (error) error->clear();
    if (staleSkipped) *staleSkipped = 0;
    if (!root.value(QStringLiteral("records")).isArray()) {
        if (error) *error = QStringLiteral("Embedding store root is missing its records array.");
        return {};
    }

    QHash<QString, VibeCutMediaDocument> currentById;
    for (const VibeCutMediaDocument &document : documents) {
        if (document.kind != QLatin1String("transcript") && document.kind != QLatin1String("ocr_text")) continue;
        if (document.id.trimmed().isEmpty()) continue;
        if (currentById.contains(document.id)) {
            if (error) *error = QStringLiteral("Canonical media documents contain duplicate id '%1'.").arg(document.id);
            return {};
        }
        currentById.insert(document.id, document);
    }

    QJsonArray filteredRecords;
    int skipped = 0;
    for (const QJsonValue &value : root.value(QStringLiteral("records")).toArray()) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("Embedding store contains a non-object record.");
            return {};
        }
        VibeCutEmbeddingRecord record;
        QString recordError;
        if (!VibeCutEmbeddingRecord::fromJson(value.toObject(), record, &recordError)) {
            if (error) *error = recordError;
            return {};
        }
        const bool targetSpace = record.model == kModel &&
                                 record.modelRevision == kModelRevision &&
                                 record.producerId == kProducer &&
                                 record.producerVersion == kProducerVersion &&
                                 record.modality == QLatin1String("text");
        if (!targetSpace) continue;

        const auto currentIt = currentById.constFind(record.anchorId);
        if (currentIt == currentById.constEnd()) {
            ++skipped;
            continue;
        }
        const VibeCutMediaDocument &document = currentIt.value();
        const QString storedHash = record.metadata.value(QStringLiteral("text_sha256")).toString().trimmed();
        const QString currentHash = textHash(document.text.trimmed());
        const QString currentSourceId = document.metadata.value(QStringLiteral("source_id")).toString();
        const QString currentFingerprint = document.metadata.value(QStringLiteral("source_fingerprint")).toString();
        const bool current = record.anchorKind == document.kind &&
                             record.startFrame == document.startFrame &&
                             record.endFrame == document.endFrame &&
                             record.sourceId == currentSourceId &&
                             record.sourceFingerprint == currentFingerprint &&
                             !storedHash.isEmpty() && storedHash == currentHash;
        if (!current) {
            ++skipped;
            continue;
        }
        filteredRecords.append(value);
    }

    QJsonObject filteredRoot = root;
    filteredRoot.insert(QStringLiteral("records"), filteredRecords);
    if (staleSkipped) *staleSkipped = skipped;
    return filteredRoot;
}
