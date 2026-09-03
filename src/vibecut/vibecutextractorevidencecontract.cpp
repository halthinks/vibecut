/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutextractorevidencecontract.h"

#include <QJsonValue>
#include <QStringList>

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool withinRequestedRange(const VibeCutMediaEvidenceRecord &record, int startFrame, int endFrame)
{
    if (record.startFrame < 0 && record.endFrame < 0) return true;
    if (record.startFrame < 0 || record.endFrame < 0) return false;
    return record.startFrame >= startFrame && record.endFrame <= endFrame && record.endFrame >= record.startFrame;
}
}

bool validateVibeCutExtractorEvidenceContract(const QString &capability,
                                              int requestedStartFrame,
                                              int requestedEndFrame,
                                              const QList<VibeCutMediaEvidenceRecord> &records,
                                              QString *error)
{
    if (error) error->clear();
    const QString normalizedCapability = capability.trimmed().toLower();
    if (normalizedCapability.isEmpty()) return fail(error, QStringLiteral("Extractor evidence contract requires a capability."));
    if (requestedStartFrame < 0 || requestedEndFrame < requestedStartFrame) {
        return fail(error, QStringLiteral("Extractor evidence contract received invalid authoritative request bounds."));
    }

    for (const VibeCutMediaEvidenceRecord &record : records) {
        if (!withinRequestedRange(record, requestedStartFrame, requestedEndFrame)) {
            return fail(error, QStringLiteral("Provider evidence range [%1,%2) falls outside authoritative request bounds [%3,%4).")
                                   .arg(record.startFrame).arg(record.endFrame).arg(requestedStartFrame).arg(requestedEndFrame));
        }

        if (normalizedCapability != QLatin1String("diarization")) continue;

        if (record.kind != QLatin1String("speaker_segment")) {
            return fail(error, QStringLiteral("Diarization providers may persist only 'speaker_segment' evidence records."));
        }
        if (record.startFrame < 0 || record.endFrame <= record.startFrame) {
            return fail(error, QStringLiteral("Diarization speaker segments require an exact non-empty frame range."));
        }
        const QString clusterId = record.metadata.value(QStringLiteral("speaker_cluster_id")).toString().trimmed();
        if (clusterId.isEmpty()) {
            return fail(error, QStringLiteral("Diarization speaker segments require metadata.speaker_cluster_id."));
        }
        if (clusterId.size() > 128) {
            return fail(error, QStringLiteral("Diarization speaker_cluster_id may not exceed 128 characters."));
        }
        if (record.metadata.contains(QStringLiteral("overlap")) && !record.metadata.value(QStringLiteral("overlap")).isBool()) {
            return fail(error, QStringLiteral("Diarization metadata.overlap must be boolean when present."));
        }
        if (record.metadata.contains(QStringLiteral("channel"))) {
            const QJsonValue channel = record.metadata.value(QStringLiteral("channel"));
            if (!channel.isDouble() || channel.toInt(-1) < 0) {
                return fail(error, QStringLiteral("Diarization metadata.channel must be a non-negative integer when present."));
            }
        }

        // A diarizer has clustering authority, not human-identity authority.
        // Names/entities are stored only through the user-governed speaker
        // association layer, never promoted from extractor output.
        const QStringList forbiddenIdentityKeys{
            QStringLiteral("speaker_name"),
            QStringLiteral("display_name"),
            QStringLiteral("person_id"),
            QStringLiteral("identity_id"),
            QStringLiteral("speaker_entity_id"),
        };
        for (const QString &key : forbiddenIdentityKeys) {
            if (record.metadata.contains(key)) {
                return fail(error, QStringLiteral("Diarization providers may cluster speakers but may not assert identity metadata '%1'.").arg(key));
            }
        }
    }
    return true;
}
