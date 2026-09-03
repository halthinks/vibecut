/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
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

    /** Replace one producer/model slice for an exact anchor. A changed source
     * fingerprint therefore produces a distinct current record rather than
     * silently reusing a stale vector. */
    static bool upsertForProjectUrl(const QUrl &projectUrl,
                                    const VibeCutEmbeddingRecord &record,
                                    QString *error = nullptr);
    static bool upsertCurrent(const VibeCutEmbeddingRecord &record, QString *error = nullptr);

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
