/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QString>

class VibeCutToolSurface;

/** Rank object-continuity tracks as reviewable editorial subject candidates.
 * Scores are transparent heuristics over prediction confidence, sampled
 * persistence, normalized screen area and center proximity; they are never
 * person/object identity claims.
 */
QJsonArray buildVibeCutSubjectCandidates(const QJsonArray &records,
                                         const QString &sourceId = QString(),
                                         const QString &label = QString(),
                                         double minScore = 0.0,
                                         double minIou = 0.25,
                                         int maxGapSteps = 2,
                                         int minObservations = 2,
                                         int limit = 20);

bool registerVibeCutSubjectCandidateTools(VibeCutToolSurface &surface, QString *error = nullptr);
