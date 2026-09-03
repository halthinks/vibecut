/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QString>

class VibeCutToolSurface;

/** Merge repeated same-label action predictions into provenance-safe summaries.
 * Scores retain their original model-relative semantics; summaries do not
 * become observed action facts or continuous-frame observations.
 */
QJsonArray buildVibeCutActionSummaries(const QJsonArray &records,
                                       const QString &sourceId = QString(),
                                       const QString &label = QString(),
                                       double minScore = 0.0,
                                       int maxGapFrames = 0,
                                       int minWindows = 1);

bool registerVibeCutActionSummaryTools(VibeCutToolSurface &surface, QString *error = nullptr);
