/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QString>

class VibeCutToolSurface;

QJsonArray buildVibeCutAudioEventTracks(const QJsonArray &records,
                                        const QString &sourceId,
                                        const QString &labelQuery,
                                        double minScore,
                                        int maxRank,
                                        int maxGapFrames);

bool registerVibeCutAudioEventSummaryTools(VibeCutToolSurface &surface, QString *error = nullptr);
