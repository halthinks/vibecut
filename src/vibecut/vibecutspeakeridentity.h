/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrl>

struct VibeCutSpeakerClusterKey
{
    QString sourceId;
    QString sourceFingerprint;
    QString extractorId;
    QString extractorVersion;
    QString speakerClusterId;

    bool valid() const;
    QString stableKey() const;
    QJsonObject toJson() const;
};

/** Project-local, user-governed mapping from anonymous diarization clusters to
 * human-readable speaker entities.
 *
 * Extractors never write this file. Associations include source fingerprint
 * and extractor version so a changed source/model result does not silently
 * inherit an old human identity assertion.
 */
class VibeCutSpeakerIdentityStore
{
public:
    static constexpr int SchemaVersion = 1;
    static constexpr int MaxEntities = 1000;
    static constexpr int MaxAssociations = 20000;
    static constexpr qint64 MaxBytes = 4LL * 1024LL * 1024LL;

    static QString fileName();
    static QJsonObject emptyRoot();
    static QJsonObject loadForProjectUrl(const QUrl &projectUrl, QString *error = nullptr);
    static QJsonObject loadCurrent(QString *error = nullptr);

    static bool upsertEntityForProjectUrl(const QUrl &projectUrl,
                                          const QString &entityId,
                                          const QString &displayName,
                                          QString *resolvedEntityId = nullptr,
                                          QString *error = nullptr);
    static bool upsertEntityCurrent(const QString &entityId,
                                    const QString &displayName,
                                    QString *resolvedEntityId = nullptr,
                                    QString *error = nullptr);

    static bool assignClusterForProjectUrl(const QUrl &projectUrl,
                                           const VibeCutSpeakerClusterKey &cluster,
                                           const QString &entityId,
                                           QString *error = nullptr);
    static bool assignClusterCurrent(const VibeCutSpeakerClusterKey &cluster,
                                     const QString &entityId,
                                     QString *error = nullptr);

    static bool unassignClusterForProjectUrl(const QUrl &projectUrl,
                                             const VibeCutSpeakerClusterKey &cluster,
                                             QString *error = nullptr);
    static bool unassignClusterCurrent(const VibeCutSpeakerClusterKey &cluster,
                                       QString *error = nullptr);

    /** Resolve one exact cluster key against an already-loaded root. Returns an
     * empty object when unassigned. */
    static QJsonObject resolve(const QJsonObject &root, const VibeCutSpeakerClusterKey &cluster);
};
