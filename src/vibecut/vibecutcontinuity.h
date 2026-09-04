/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

class VibeCutToolSurface;

/** Analyze structural continuity of one exact rough-cut candidate-ID sequence.
 * Findings are review candidates only: chronology reversals, overlapping source
 * ranges, repeated full-text hashes, provenance changes and ranked positive
 * source gaps. No normative quality threshold or mutation authority is applied. */
QJsonObject analyzeVibeCutRoughCutContinuity(const QJsonObject &context,
                                             const QJsonArray &selectedCandidateIds,
                                             quint64 currentRevision,
                                             QString *error = nullptr);

bool registerVibeCutContinuityTools(VibeCutToolSurface &surface, QString *error = nullptr);
