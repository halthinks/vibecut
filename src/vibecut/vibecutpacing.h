/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include "vibecutmediaevidence.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QtGlobal>

class VibeCutToolSurface;

/** Analyze exact current-source evidence using descriptive frame-domain
 * statistics only. No normative good/bad pacing threshold is implied. */
QJsonObject analyzeVibeCutSourcePacing(const QList<VibeCutMediaEvidenceRecord> &records,
                                       const QString &sourceId,
                                       const QString &sourceFingerprint,
                                       QString *error = nullptr);

/** Analyze one validated candidate-ID sequence from an exact rough-cut context.
 * This measures segment-duration rhythm/density and chronology/overlap state;
 * it grants no proposal execution authority. */
QJsonObject analyzeVibeCutRoughCutPacing(const QJsonObject &context,
                                         const QJsonArray &selectedCandidateIds,
                                         quint64 currentRevision,
                                         QString *error = nullptr);

bool registerVibeCutPacingTools(VibeCutToolSurface &surface, QString *error = nullptr);
