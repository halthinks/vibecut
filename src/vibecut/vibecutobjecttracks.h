/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QString>

class VibeCutToolSurface;

/** Deterministically associate sampled-frame object predictions into
 * provenance-safe visual continuity tracks.
 *
 * Returned tracks are derived prediction summaries, never object identity and
 * never evidence that an object existed on unsampled frames.
 */
QJsonArray buildVibeCutObjectTracks(const QJsonArray &records,
                                    const QString &sourceId = QString(),
                                    const QString &label = QString(),
                                    double minScore = 0.0,
                                    double minIou = 0.25,
                                    int maxGapSteps = 1,
                                    int minObservations = 2);

bool registerVibeCutObjectTrackTools(VibeCutToolSurface &surface, QString *error = nullptr);
