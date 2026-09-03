/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QString>

class VibeCutToolSurface;

QJsonArray buildVibeCutRoomToneCandidates(const QJsonArray &records,
                                          const QString &sourceId = QString(),
                                          double minMomentaryLufs = -65.0,
                                          double maxMomentaryLufs = -25.0,
                                          double maxSpreadLu = 5.0,
                                          int minObservations = 4);

bool registerVibeCutRoomToneTools(VibeCutToolSurface &surface, QString *error = nullptr);
