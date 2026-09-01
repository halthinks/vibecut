/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>

struct VibeCutMediaEvidenceRecord
{
    QString id;
    QString sourceId;
    QString sourceFingerprint;
    QString extractorId;
    QString extractorVersion;
    QString kind;
    int startFrame = -1;
    int endFrame = -1;
    QString text;
    double confidence = -1.0;
    QString producedUtc;
    QJsonObject metadata;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, VibeCutMediaEvidenceRecord &record, QString *error = nullptr);
};

class VibeCutMediaEvidence
{
public:
    static constexpr int SchemaVersion = 1;
    static constexpr int MaxRecords = 100000;
    static constexpr qint64 MaxBytes = 64LL * 1024LL * 1024LL;

    static QString fileName();
    static QJsonArray loadForProjectUrl(const QUrl &projectUrl, QString *error = nullptr);
    static QJsonArray loadCurrent(QString *error = nullptr);

    /** Replace all records for one source/extractor with one fresh extractor result set.
     * Every replacement record must use the same source id/fingerprint/extractor/version.
     * This is the canonical incremental-refresh operation for derived media evidence.
     */
    static bool replaceSourceExtractorCurrent(const QString &sourceId,
                                              const QString &sourceFingerprint,
                                              const QString &extractorId,
                                              const QString &extractorVersion,
                                              const QList<VibeCutMediaEvidenceRecord> &records,
                                              QString *error = nullptr);

    static bool clearCurrent(QString *error = nullptr);
};
