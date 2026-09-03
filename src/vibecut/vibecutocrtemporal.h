/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>

class QString;
class VibeCutToolSurface;

QJsonArray buildVibeCutOcrTemporalTracks(const QJsonArray &records,
                                         double minTextSimilarity = 0.85,
                                         double minBoxIou = 0.25,
                                         int maxMissingSamples = 0,
                                         int minObservations = 2);

bool registerVibeCutOcrTemporalTools(VibeCutToolSurface &surface, QString *error = nullptr);
