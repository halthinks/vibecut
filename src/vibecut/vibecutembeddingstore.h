/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

struct VibeCutEmbeddingRecord
{
    QString id;
    QString anchorKind;
    QString anchorId;
    QString sourceId;
    QString sourceFingerprint;
    QString modality;
    QString model;
    QString modelRevision;
    QString producerId;
    QString producerVersion;
    int startFrame = -1;
    int endFrame = -1;
    QVector<double> vector;
    QString producedUtc;
    QJsonObject metadata;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, VibeCutEmbeddingRecord &record, QString *error = nullptr);
};

struct VibeCutEmbeddingSearchHit
{
    QString embeddingId;
    QString anchorKind;
    QString anchorId;
    QString sourceId;
    QString sourceFingerprint;
    QString modality;
    int startFrame = -1;
    int endFrame = -1;
    double similarity = -1.0;
    QJsonObject metadata;

    QJsonObject toJson() const;
};

/** Project-local semantic-vector sidecar.
 *
 * Raw embeddings are intentionally kept out of .vibecutmedia.json. That ledger
 * remains human-inspectable evidence; this store is a bounded search index with
 * exact anchor/source/model provenance. Records are unit-normalized on
 * admission and cosine comparisons are allowed only inside one model revision
 * and dimension.
 */
class VibeCutEmbeddingStore
{
public:
    static constexpr int SchemaVersion = 1;
    static constexpr int MaxRecords = 10000;
    static constexpr int MaxDimensions = 4096;
    static constexpr qint64 MaxBytes = 128LL * 1024LL * 1024LL;

    static QString fileName();
    static QJsonObject emptyRoot();
    static QJsonObject loadForProjectUrl(const QUrl &projectUrl, QString *error = nullptr);
    static QJsonObject loadCurrent(QString *error = nullptr);

    /** Replace one exact anchor/source/model/producer slice. A changed source
     * fingerprint therefore remains a distinct record until a full refresh
     * replaces the producer/model slice. */
    static bool upsertForProjectUrl(const QUrl &projectUrl,
                                    const VibeCutEmbeddingRecord &record,
                                    QString *error = nullptr);
    static bool upsertCurrent(const VibeCutEmbeddingRecord &record, QString *error = nullptr);

    /** Atomically replace all current records emitted by one producer for one
     * exact embedding model revision. This is the authoritative refresh path:
     * stale fingerprints/removed anchors from the old slice disappear in the
     * same commit that installs the new current vectors. */
    static bool replaceProducerModelForProjectUrl(const QUrl &projectUrl,
                                                  const QString &producerId,
                                                  const QString &producerVersion,
                                                  const QString &model,
                                                  const QString &modelRevision,
                                                  const QList<VibeCutEmbeddingRecord> &records,
                                                  QString *error = nullptr);
    static bool replaceProducerModelCurrent(const QString &producerId,
                                            const QString &producerVersion,
                                            const QString &model,
                                            const QString &modelRevision,
                                            const QList<VibeCutEmbeddingRecord> &records,
                                            QString *error = nullptr);

    static QList<VibeCutEmbeddingSearchHit> cosineSearch(const QJsonObject &root,
                                                         const QVector<double> &unitQuery,
                                                         const QString &model,
                                                         const QString &modelRevision,
                                                         const QStringList &modalities = QStringList(),
                                                         int limit = 25,
                                                         double minSimilarity = -1.0,
                                                         QString *error = nullptr);

    static bool normalizeVector(const QVector<double> &input, QVector<double> &unit, QString *error = nullptr);
};
