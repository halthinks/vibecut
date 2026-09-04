/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

class VibeCutToolSurface;

/** Analyze an exact rough-cut candidate sequence. Semantic vectors are optional
 * model representations supplied by the caller/tool layer; lexical continuity
 * remains available when exact current embeddings are absent. Output is
 * relative derived analysis only, never a narrative truth or edit authority. */
QJsonObject analyzeVibeCutNarrativeSequence(const QJsonObject &context,
                                            const QJsonArray &selectedCandidateIds,
                                            const QJsonObject &semanticVectorsByCandidate,
                                            quint64 currentRevision,
                                            int maxBoundaryCandidates = 5,
                                            int maxRepetitionCandidates = 10,
                                            QString *error = nullptr);

bool registerVibeCutNarrativeTools(VibeCutToolSurface &surface, QString *error = nullptr);
